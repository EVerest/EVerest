# The EVerest crates need Rust 1.86 or newer; kirkstone ships 1.59 and scarthgap 1.75.
# The kirkstone/rust and scarthgap/rust branches of meta-lts-mixins
# (https://git.yoctoproject.org/meta-lts-mixins) provide a current toolchain under poky's
# recipe names, so cargo_common and its siblings work unchanged. Later releases ship a new
# enough Rust themselves.

EVEREST_RUST_MIN_VERSION = "1.86.0"

# kirkstone has cargo-update-recipe-crates only through meta-lts-mixins; without it
# Rust is too old anyway and the recipe is skipped below, so it must still parse.
def everest_rust_update_crates_class(d):
    for classdir in ('classes-recipe', 'classes'):
        if bb.utils.which(d.getVar('BBPATH'), os.path.join(classdir, 'cargo-update-recipe-crates.bbclass')):
            return 'cargo-update-recipe-crates'
    return ''

inherit ${@everest_rust_update_crates_class(d)}

python () {
    version = (d.getVar('RUSTVERSION') or '0').rstrip('%')
    minimum = d.getVar('EVEREST_RUST_MIN_VERSION')
    if bb.utils.vercmp_string_op(version, minimum, '<'):
        raise bb.parse.SkipRecipe("Rust %s is too old for the EVerest crates, %s or newer is needed;"
                                  " add the <release>/rust layer of meta-lts-mixins" % (version, minimum))
}
