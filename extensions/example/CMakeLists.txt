# extensions/example/CMakeLists.txt
# The sample extension (task 0018): built against the SDK only.

openeden_add_extension(example
    SOURCES
        src/example_extension.cpp
    MANIFEST
        extension.json
    DATA
        data
)
