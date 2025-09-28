#!/bin/bash

# Provide a script even though clang-format is run automatically because
# clang-format interacts oddly with reflection at times

for i in include/cpp_dyn/*; do
   clang-format -i "${i}"
done
