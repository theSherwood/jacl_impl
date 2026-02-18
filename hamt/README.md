# Test

```
cc -O2 -Wall test_hamt.c && ./a.out
```

# TODO

- [x] Accept keys that aren't strings
  - [x] Requires custom hash and comparison functions
  - [x] Caller is responsible for freeing keys
- [x] Accept a finalizer/decrement function for values of removed nodes
  - [x] Callers might want to decrement ref counts of values
- [x] `hamt_has`
- [x] iterators
  - [x] values
  - [x] keys
  - [x] pairs
- [ ] Support transients
- [x] cache hamt_count
- [ ] cache an xor key
- [ ] Support an alternative to ref_count?
  - Some callers might want to use garbage collection for everything?
  - This could be done using includes and monomorphization
