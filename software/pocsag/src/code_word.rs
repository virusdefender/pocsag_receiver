use super::bch::bch_repair;

#[derive(Debug, PartialEq, Copy, Clone)]
pub struct AddressCodeWord {
    pub address: u32,
    pub func: u8,
}

#[derive(Debug, PartialEq, Copy, Clone)]
pub struct MessageCodeWord {
    pub data: u32,
}

#[derive(Debug, PartialEq, Copy, Clone)]
pub enum CodeWord {
    Address(AddressCodeWord),
    Message(MessageCodeWord),
    Idle,
}

#[derive(Debug)]
pub enum RawCodeWord {
    BCHOk(CodeWord, Option<u32>),
    BCHFailed(u32),

    Idle,
}

const POCSAG_IDLE_WORD: u32 = 0x7A89C197;

impl RawCodeWord {
    pub fn parse(bytes: [u8; 4], frame_index: usize) -> Self {
        let raw = u32::from_be_bytes(bytes);

        if raw == POCSAG_IDLE_WORD {
            return RawCodeWord::Idle;
        }

        match bch_repair(raw) {
            Ok(repaired) => {
                let flag = (repaired >> 31) & 1;
                let codeword = if flag == 0 {
                    let address = (repaired >> 13) & 0x3FFFF;
                    CodeWord::Address(AddressCodeWord {
                        address: address << 3 | (frame_index as u32),
                        func: ((repaired >> 11) & 0x3) as u8,
                    })
                } else {
                    CodeWord::Message(MessageCodeWord { data: (repaired >> 11) & 0xFFFFF })
                };
                if repaired == raw {
                    RawCodeWord::BCHOk(codeword, None)
                } else {
                    RawCodeWord::BCHOk(codeword, Some(raw))
                }
            }
            Err(_) => RawCodeWord::BCHFailed(raw),
        }
    }

    pub fn is_address(&self, address: u32) -> bool {
        if let RawCodeWord::BCHOk(CodeWord::Address(addr), _) = self { addr.address == address } else { false }
    }
}

fn bit_rev(n: u8) -> u8 {
    ((n & 1) << 3) | ((n & 2) << 1) | ((n & 4) >> 1) | ((n & 8) >> 3)
}

pub struct CodeWordReader<'a> {
    cw: &'a [RawCodeWord],
    nib_pos: usize,
}

impl<'a> CodeWordReader<'a> {
    pub fn new(cw: &'a [RawCodeWord]) -> Self {
        CodeWordReader { cw, nib_pos: 0 }
    }

    pub fn read_nibble(&mut self) -> Option<u8> {
        let cw_idx = self.nib_pos / 5;
        if cw_idx >= self.cw.len() {
            return None;
        }
        let nib_in_cw = self.nib_pos % 5;
        self.nib_pos += 1;
        match &self.cw[cw_idx] {
            RawCodeWord::BCHOk(CodeWord::Message(msg), _) => {
                Some(bit_rev(((msg.data >> ((4 - nib_in_cw) * 4)) & 0xF) as u8))
            }
            _ => None,
        }
    }

    pub fn skip(&mut self, count: usize) {
        for _ in 0..count {
            self.read_nibble();
        }
    }

    pub fn read_bytes(&mut self, count: usize) -> Option<Vec<u8>> {
        let mut bytes = Vec::with_capacity(count / 2);
        for _ in 0..count / 2 {
            let hi = self.read_nibble()?;
            let lo = self.read_nibble()?;
            bytes.push(hi << 4 | lo);
        }
        Some(bytes)
    }

    pub fn read_geo_coord(&mut self, deg_digits: usize) -> Option<f64> {
        let deg = self.read_bcd_u32(deg_digits)?;
        let min = self.read_bcd_u32(2)?;
        let frac = self.read_bcd_u32(4)?;
        Some(deg as f64 + min as f64 / 60.0 + frac as f64 / 60000.0)
    }

    pub fn read_bcd_u32(&mut self, count: usize) -> Option<u32> {
        let mut val: u32 = 0;
        let mut has_digit = false;
        let mut failed = false;
        for _ in 0..count {
            match self.read_nibble() {
                None => return None,
                Some(n) if n <= 9 => {
                    val = val * 10 + n as u32;
                    has_digit = true;
                }
                Some(n) if n == 0xC => {
                    // space padding
                }
                Some(_) => {
                    failed = true;
                }
            }
        }
        if failed || !has_digit {
            return None;
        }
        Some(val)
    }

    pub fn read_bcd_i32(&mut self, count: usize) -> Option<i32> {
        let mut val: i32 = 0;
        let mut negative = false;
        let mut has_digit = false;
        let mut failed = false;
        for _ in 0..count {
            match self.read_nibble() {
                None => return None,
                Some(n) if n <= 9 => {
                    val = val * 10 + n as i32;
                    has_digit = true;
                }
                Some(n) if n == 0xC => {
                    // space padding
                }
                Some(n) if !has_digit && n == 0xD => {
                    negative = true;
                }
                Some(_) => {
                    failed = true;
                }
            }
        }
        if failed || !has_digit {
            return None;
        }
        Some(if negative { -val } else { val })
    }
}
