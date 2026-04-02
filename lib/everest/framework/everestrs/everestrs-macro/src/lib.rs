use proc_macro::TokenStream;
use quote::quote;
use syn::{parse_macro_input, spanned::Spanned, ItemFn, Type};

/// Attribute macro that wraps an EVerest module's `main` function to control
/// module lifecycle. The user function receives a borrowed `&Module`, ensuring
/// it cannot be dropped prematurely. After the user function returns, the
/// `Module` is dropped deterministically before process exit.
///
/// # Example
///
/// ```ignore
/// #[everestrs::main]
/// fn main(module: &Module) {
///     let class = Arc::new(MyModule {});
///     let _publishers = module.start(class.clone(), class.clone());
///     loop { std::thread::sleep(std::time::Duration::from_secs(1)); }
/// }
/// ```
#[proc_macro_attribute]
pub fn main(attr: TokenStream, item: TokenStream) -> TokenStream {
    if !attr.is_empty() {
        let attr2: proc_macro2::TokenStream = attr.into();
        return syn::Error::new_spanned(attr2, "#[everestrs::main] takes no arguments")
            .to_compile_error()
            .into();
    }

    let input = parse_macro_input!(item as ItemFn);

    if let Err(e) = main_validate(&input) {
        return e.to_compile_error().into();
    }

    let sig = &input.sig;
    let param = &sig.inputs[0];
    let body = &input.block;
    let ret = &sig.output;

    // Extract the parameter name and inner type from `name: &Type`.
    let (param_name, inner_ty) = match param {
        syn::FnArg::Typed(pat_type) => {
            let name = &pat_type.pat;
            match pat_type.ty.as_ref() {
                Type::Reference(type_ref) => (name, &type_ref.elem),
                _ => unreachable!("validated above"),
            }
        }
        _ => unreachable!("validated above"),
    };

    let expanded = quote! {
        fn main() #ret {
            let #param_name = #inner_ty::new();
            let __everest_result = {
                fn __everest_main(#param_name: &#inner_ty) #ret
                    #body
                __everest_main(&#param_name)
            };
            drop(#param_name);
            __everest_result
        }
    };

    expanded.into()
}

fn main_validate(input: &ItemFn) -> Result<(), syn::Error> {
    if input.sig.ident != "main" {
        return Err(syn::Error::new(
            input.sig.ident.span(),
            "#[everestrs::main] can only be applied to `fn main`",
        ));
    }

    if input.sig.asyncness.is_some() {
        return Err(syn::Error::new(
            input.sig.asyncness.span(),
            "#[everestrs::main] does not support async functions",
        ));
    }

    if !input.sig.generics.params.is_empty() {
        return Err(syn::Error::new(
            input.sig.generics.span(),
            "#[everestrs::main] does not support generic functions",
        ));
    }

    if input.sig.inputs.len() != 1 {
        return Err(syn::Error::new(
            input.sig.inputs.span(),
            "#[everestrs::main] function must have exactly one parameter: `module: &Module`",
        ));
    }

    let param = &input.sig.inputs[0];
    match param {
        syn::FnArg::Receiver(_) => {
            return Err(syn::Error::new(
                param.span(),
                "#[everestrs::main] function must not take `self`",
            ));
        }
        syn::FnArg::Typed(pat_type) => match pat_type.ty.as_ref() {
            Type::Reference(type_ref) => {
                if type_ref.mutability.is_some() {
                    return Err(syn::Error::new(
                        type_ref.mutability.span(),
                        "#[everestrs::main] parameter must be a shared reference (`&Module`), not `&mut`",
                    ));
                }
            }
            _ => {
                return Err(syn::Error::new(
                    pat_type.ty.span(),
                    "#[everestrs::main] parameter must be a reference (e.g. `module: &Module`)",
                ));
            }
        },
    }

    Ok(())
}
