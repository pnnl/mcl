use libmcl_sys::*;

#[test]
fn init() {
    unsafe {
        assert_eq!(mcl_init(1, MCL_NULL.into()), 0);

        assert_eq!(mcl_finit(), 0);
    }
}
