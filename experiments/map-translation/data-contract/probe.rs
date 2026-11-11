#![forbid(unsafe_code)]
use rust_raycaster_poc::{admission, Raycaster};

// Load retained C++ exports through the unmodified production PoC reader/core.
fn main() {
    let a: Vec<_> = std::env::args().collect();
    assert_eq!(a.len(), 4);
    for _ in 0..64 {
        let mut engine = admission::load(&a[1], &a[2], None, || {}).unwrap();
        assert!(engine.bounds().is_none());
        for _ in 0..32 {
            for (start, end) in rays() {
                assert!(engine.segment(start, end).unwrap().is_none());
            }
        }
        for bad in [f32::from_bits(0xffc00000), f32::INFINITY, f32::NEG_INFINITY] {
            assert!(engine.segment([bad, 0., 0.], [0.; 3]).is_err());
            assert!(engine.segment([0.; 3], [0., bad, 0.]).is_err());
        }
    }
    println!("{}: valid empty; bounds=None; 64 load/drop cycles; 14336 finite no-hits; 384 invalid endpoint errors", a[3]);
}
fn rays() -> [([f32; 3], [f32; 3]); 7] {
    [
        ([0.; 3], [0.; 3]),
        ([0.; 3], [1e-6, 0., 0.]),
        ([0., 0., 1.], [0., 0., -1.]),
        ([-1., -0., 0.], [1., 0., 0.]),
        ([0., -1., 0.], [0., 1., 0.]),
        ([-1.; 3], [1.; 3]),
        ([-1e8, 0., 0.], [1e8, 0., 0.]),
    ]
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn nonfinite_each_component_including_raw_asset_bits() {
        for bits in [0xffc00000, 0x7fc00000, 0x7f800000, 0xff800000] {
            for axis in 0..3 {
                let mut v = vec![[0., 0., 0.], [4., 0., 0.], [0., 4., 0.]];
                v[1][axis] = f32::from_bits(bits);
                for indices in [vec![], vec![0, 1, 2]] {
                    let error = Raycaster::build(v.clone(), indices).err().unwrap();
                    assert_eq!(error.0, "nonfinite or out-of-bound vertex");
                }
            }
        }
    }
    #[test]
    fn finite_controls_and_coordinate_limit() {
        for x in [-1e8, 1e8, -0., 0., f32::from_bits(1)] {
            assert!(Raycaster::build(vec![[x, 0., 0.]], vec![]).is_ok());
        }
        for x in [f32::from_bits(1e8f32.to_bits() + 1), f32::MAX] {
            assert!(Raycaster::build(vec![[x, 0., 0.]], vec![]).is_err());
        }
        let mut e = Raycaster::build(vec![[0., 0., 0.], [4., 0., 0.], [0., 4., 0.]], vec![0, 1, 2]).unwrap();
        assert!(e.bounds().is_some());
        assert!(e.segment([1., 1., 2.], [1., 1., -2.]).unwrap().is_some());
    }
}
