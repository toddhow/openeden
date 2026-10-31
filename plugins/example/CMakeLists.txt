# plugins/example/CMakeLists.txt
# The sample plugin (task 0018): built against the SDK only.

openeden_add_plugin(example
    SOURCES
        src/example_plugin.cpp
    MANIFEST
        plugin.json
    DATA
        data
)
