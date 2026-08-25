//! decode_check — decode a `.svmb` with the exact `temen_encode` the cdylib uses and print the
//! real error. The cdylib's `temen_run_onramp` maps any decode failure to `STATUS_DECODE_ERR`
//! (status=1); this surfaces the underlying reason (BadVersion / BadOpcode / section mismatch / …).
//!
//!   decode_check <image.svmb>

fn main() {
    let path = std::env::args().nth(1).expect("usage: decode_check <image.svmb>");
    let bytes = std::fs::read(&path).expect("read image");
    println!("{}: {} bytes; magic+ver {:02x?}", path, bytes.len(), &bytes[..bytes.len().min(6)]);
    match temen_encode::decode_module(&bytes) {
        Ok(m) => println!("decode_module: OK ({} funcs)", m.funcs.len()),
        Err(e) => println!("decode_module: ERR {e:?}"),
    }
    match temen_encode::decode_unit(&bytes) {
        Ok(_) => println!("decode_unit: OK"),
        Err(e) => println!("decode_unit: ERR {e:?}"),
    }
}
