# NetBuffer container serialization

`NetBuffer` supports `<<` / `>>` for `vector`, `set`, `map`, `unordered_set`,
and `unordered_map`. Both mutable and const containers can be sent. Receive
destinations must be mutable. Existing `list` serialization is unchanged.
All list overloads, including string and wide-string lists, are located in
`NetBufferContainerOperators.inl`; they retain their legacy encoding and
append-on-read behavior rather than using the new recursive container path.

| Container | Wire layout |
| --- | --- |
| vector | uint32 count, elements in sequence |
| set | byte direction, uint32 count, elements |
| map | byte direction, uint32 count, key/value pairs |
| unordered_set | uint32 count, elements |
| unordered_map | uint32 count, key/value pairs |

Direction is 0 for `std::less<Key>` and 1 for `std::greater<Key>`. These are
the only supported ordered-container comparators. The received direction must
match the destination type, including for empty containers. Other directions
and mismatches fail; deserialization does not change a container's comparator.
Unordered containers preserve values, not iteration order or bucket layout.
Receiver hash/equality policies must agree with the sender's key equivalence.

Elements and map keys/values may be arithmetic types, `std::string`,
`std::wstring`, or recursively one of the five new container types.
`vector<bool>` encodes each value as one byte, 0 or 1. User-defined structs,
pointers, and nested legacy lists are not supported by the new recursive path.
Unsupported element types and comparators fail at compilation, rather than
falling back to copying object memory.

Numeric encoding retains the existing native byte order and type widths;
this is not a portable endian-normalized protocol. Strings retain the existing
16-bit byte-length prefix and Windows wide-character format. New container
counts are 32-bit; legacy lists still use `size_t`.

Container writes check the packet limit and grow the allocated buffer when
necessary. Reads reject excessive counts, truncation, invalid boolean values,
odd wide-string byte lengths, and duplicate set elements/map keys. No allocation
is reserved directly from an untrusted count. Counts cannot exceed the remaining
bytes, since each supported element consumes at least one byte.

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
