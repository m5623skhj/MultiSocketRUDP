# NetBuffer container serialization

`NetBuffer` supports `<<` / `>>` for `vector`, `set`, `map`, `unordered_set`,
and `unordered_map`. Both mutable and const containers can be sent. Receive
destinations must be mutable. Existing leaf `list` serialization is unchanged.
All list overloads, including string and wide-string lists, are located in
`NetBufferContainerOperators.inl`. Mutable leaf-list operators retain their
legacy encoding and append-on-read behavior. Lists of generated structs/nested
containers, and lists used through `WriteValue`/`ReadValue`, use checked recursive
serialization and replace-on-success reads, retaining the `size_t` wire count.

| Container | Wire layout |
| --- | --- |
| vector | uint32 count, elements in sequence |
| set | byte direction, uint32 count, elements |
| map | byte direction, uint32 count, key/value pairs |
| unordered_set | uint32 count, elements |
| unordered_map | uint32 count, key/value pairs |
| list | size_t count, elements in sequence |

Direction is 0 for `std::less<Key>` and 1 for `std::greater<Key>`. These are
the only supported ordered-container comparators. The received direction must
match the destination type, including for empty containers. Other directions
and mismatches fail; deserialization does not change a container's comparator.
Unordered containers preserve values, not insertion order, iteration order, or
bucket layout. A receiver can therefore iterate the same values in a different
order, and the same logical contents can produce different serialized byte
orders across processes or runs. Never use unordered-container iteration order
for positional field matching, deterministic hashes, or replayable output.
Receiver hash/equality policies must agree with the sender's key equivalence.

Elements and map values may be arithmetic types, `std::string`, `std::wstring`,
containers, or generated data structs with a `NetBufferCodec<T>` specialization.
The generator restricts map keys and set elements to scalar/string types.
`vector<bool>` encodes each value as one byte, 0 or 1. Unregistered user-defined
structs and pointers are not supported by the recursive path.
Unsupported element types and comparators fail at compilation, rather than
falling back to copying object memory.

Numeric encoding retains the existing native byte order and type widths;
this is not a portable endian-normalized protocol. Strings retain the existing
16-bit byte-length prefix and Windows wide-character format. New container
counts are 32-bit; legacy lists still use `size_t`.

Container writes check the packet limit and grow the allocated buffer when
necessary. Reads reject excessive counts, truncation, invalid boolean values,
odd wide-string byte lengths, and duplicate set elements/map keys. No allocation
is reserved directly from an untrusted count. Counts are checked against the
remaining bytes divided by the element's minimum encoded size. Empty generated
structs consume zero bytes, so only the count cap (`BUFFFER_MAX`) applies to them.

New validation failures set `GetBufferError()` to 1 (read) or 2 (write) and
throw `std::runtime_error`; underlying buffer operations retain their existing
exception types. Reads build temporary containers with the receiver's policies
and allocator, then swap on success. A decoding failure leaves the destination
unchanged but does not restore the read cursor. Failed writes can leave partial
output. Discard the packet buffer after either failure.

Callers must prevent concurrent mutation of the source while sending and
concurrent access to a shared buffer or receive destination. No internal locks
are added.

`CoreTest` includes the `NetBufferContainerTest.*` runtime tests. The adjacent
`NetBufferContainerCompileFail.cpp` fixture is intentionally excluded from the
project: from a Visual Studio x64 developer shell, compile it with
`/Zs /std:c++20 /EHsc /DUNICODE /D_UNICODE /Iexternal\CommonCode\Common` and each of
`NETBUFFER_REJECT_COMPARATOR`, `NETBUFFER_REJECT_STRUCT`, and
`NETBUFFER_REJECT_CONST_READ` must fail.
