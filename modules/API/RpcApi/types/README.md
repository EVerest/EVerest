# RpcApi types

To change the JSON-RPC types, edit `json_rpc_api.yaml`.

To generate and use an updated header in `modules/API/RpcApi/types/json_rpc_api/`:

1. Copy `json_rpc_api.yaml` into the top-level `types/` directory.
2. Create a fresh build directory (or reuse an existing one): `mkdir build-headers`.
3. (Re-)Initialize the build directory to pick up the types file: `cmake -B build-headers`.
4. Run the code generation target: `make -C build-headers generate_types_cpp_everest-core`.
5. The generated header appears at `build-headers/generated/include/generated/types/json_rpc_api.hpp`.
4. Move that header to `modules/API/RpcApi/types/json_rpc_api/`.
5. Remove the copied `json_rpc_api.yaml` (from step 1.) from the top-level `types/` directory.
6. Remove the build directory, if it wasn't reused: `rm -rf build-headers`.
7. If you reused an existing build directory, remove the build folder `build/generated` before your next compile.
8. Reformat the newly created header to conform to style guidelines: `clang-format -i modules/API/RpcApi/types/json_rpc_api/json_rpc_api.hpp`.
9. Commit both the updated YAML and the moved header to git.
