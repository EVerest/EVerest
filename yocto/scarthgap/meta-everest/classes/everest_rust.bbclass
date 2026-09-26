# The EVerest crates need Rust 1.82 or newer; scarthgap ships 1.75. The scarthgap/rust
# branch of meta-lts-mixins (https://git.yoctoproject.org/meta-lts-mixins) provides a
# current toolchain under poky's recipe names, so cargo_common and its siblings work
# unchanged. Later releases ship a new enough Rust themselves.

EVEREST_RUST_MIN_VERSION = "1.86.0"

python () {
    version = (d.getVar('RUSTVERSION') or '0').rstrip('%')
    minimum = d.getVar('EVEREST_RUST_MIN_VERSION')
    if bb.utils.vercmp_string_op(version, minimum, '<='):
        raise bb.parse.SkipRecipe("Rust %s is too old for the EVerest crates, %s or newer is needed;"
                                  " on scarthgap add the meta-lts-mixins scarthgap/rust layer" % (version, minimum))
}
