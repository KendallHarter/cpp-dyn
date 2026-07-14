#!/bin/bash

# Provide a script even though clang-format is run automatically because
# clang-format interacts oddly with reflection at times

for i in include/khct/* tests/* examples/proof_of_concept/better_syntax/* examples/proof_of_concept/better_vtable_callers/*; do
   clang-format -i "${i}"
done
