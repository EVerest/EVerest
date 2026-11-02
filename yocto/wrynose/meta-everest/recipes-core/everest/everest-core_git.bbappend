# python3-pybind11-json exports usable include directories, see
# lib/everest/framework/everestpy/src/everest/CMakeLists.txt
EXTRA_OECMAKE:remove = "-DPYBIND11_INTERFACE_INCLUDE_DIRECTORIES=${STAGING_INCDIR}/${PYTHON_DIR}"

# GCC 15 reports a false -Wmaybe-uninitialized for fixed_vector inside std::optional
# (iso15118 message conversion), which libiso15118 turns into an error with -Werror
OECMAKE_CXX_FLAGS += "-Wno-error=maybe-uninitialized"
