// Port from https://github.com/philpem/libbch_pocsag/tree/master

const BCH_GEN_POLY: u32 = 0x769;
const BCH_GEN_POLY_MSB: u32 = BCH_GEN_POLY << (32 - 10 - 1); // 0xED200000
const BCH_SYND_MASK: u32 = 0x3FF;
const BCH_GEN_HIGH: u32 = 1 << 10; // 0x400
const BCH_MEGGITT_MSB_SYNDROME: u32 = 0x3B4; // x^30 mod g(x)

// Precomputed bitmap of "MSB error" syndromes.
// Generated from g(x)=0x769; 128 bytes covering all 10-bit syndromes.
static MEGGITT_MSB_ERROR_BITMAP: [u8; 128] = [
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x40, 0x08, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x40, 0x04, 0x00, 0x00, 0x20, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
    0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x80, 0x40, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x20, 0x04,
    0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x08, 0x00, 0x00, 0x02, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x10, 0x00, 0x10, 0x00, 0x71, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
];

fn is_msb_error_syndrome(syndrome: u16) -> bool {
    (MEGGITT_MSB_ERROR_BITMAP[(syndrome >> 3) as usize] >> (syndrome & 7)) & 1 != 0
}

/// Systematic BCH(31,21) encoder.
/// `data` has 21 data bits in bits 31..11; bits 10..0 are ignored.
/// Returns the 32-bit codeword with 10 BCH parity bits (bits 10..1) and even parity (bit 0).
fn bch_encode(data: u32) -> u32 {
    let mut local_cw = data & 0xFFFFF800;
    let mut cw_e = local_cw;

    for _ in 0..21 {
        if cw_e & 0x80000000 != 0 {
            cw_e ^= BCH_GEN_POLY_MSB;
        }
        cw_e <<= 1;
    }
    local_cw |= cw_e >> 21;

    let parity = local_cw.count_ones() & 1;
    if parity != 0 { local_cw | 1 } else { local_cw }
}

/// Meggitt-type BCH(31,21) decoder. Corrects up to 2 bit errors in bits 31..1.
/// Even parity bit (bit 0) is ignored.
/// Returns `Ok(repaired)` on success, `Err(())` if uncorrectable.
pub fn bch_repair(cw: u32) -> Result<u32, ()> {
    let syndrome = ((bch_encode(cw) ^ cw) >> 1) & BCH_SYND_MASK;
    if syndrome == 0 {
        return Ok(cw);
    }

    let mut syndrome = syndrome;
    let mut result: u32 = 0;
    let mut damaged_cw = cw;

    for xbit in 0..31 {
        result <<= 1;
        if is_msb_error_syndrome(syndrome as u16) {
            syndrome ^= BCH_MEGGITT_MSB_SYNDROME;
            result |= (!damaged_cw & 0x80000000) >> 30;
        } else {
            result |= (damaged_cw & 0x80000000) >> 30;
        }
        damaged_cw <<= 1;

        syndrome <<= 1;
        if syndrome & BCH_GEN_HIGH != 0 {
            syndrome ^= BCH_GEN_POLY;
        }
        syndrome &= BCH_SYND_MASK;

        if syndrome == 0 {
            let remaining = 30 - xbit;
            if remaining > 0 {
                result = (result << remaining) | ((damaged_cw >> (32 - remaining)) << 1);
            }
            break;
        }
    }

    if syndrome != 0 { Err(()) } else { Ok(result) }
}
