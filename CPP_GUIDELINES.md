# INN++ C++ guidelines

General C++ style and class-design references for INN++, used when writing
new C++ and in the refactoring phase.  The project's rules, workflow and
open items are in [CLAUDE.md](CLAUDE.md).

## Scott Meyers, Effective C++ (3rd ed.)
- Item 13: manage resources with objects (RAII); prefer `std::unique_ptr` /
  `std::shared_ptr` over raw `new`/`delete`.
- Item 14: resource-managing classes decide between deep copy and reference
  counting; document the choice.
- Item 15: give access to the raw resource (`.get()`) so it composes with C
  APIs.
- Item 17: store `new`'s result in a smart pointer in its own statement.
- Item 3: `const` wherever correct.
- Items 7-9: virtual destructors in polymorphic bases; destructors never
  throw; no virtual calls from constructors or destructors.
- Item 20: `const T&` for parameters not modified and not cheap to copy.
- Item 23: prefer non-member, non-friend functions.
- Item 27: minimize casts; named casts only, never `(T)expr`.

## Scott Meyers, Effective Modern C++
- Items 5, 8, 9, 10: `auto` where the type is obvious; `nullptr`; `using`
  over `typedef`; `enum class`.
- Items 11, 12, 14, 15: `= delete`; `override`; `noexcept` where it cannot
  throw (moves in particular); `constexpr` over `#define`.
- Item 17: declaring a destructor or copy operation suppresses the moves.
- Items 18, 19, 21: `unique_ptr` by default, `shared_ptr` only for shared
  ownership, `make_unique`/`make_shared`.
- Item 31: no default lambda captures.

## Michael Caisse, The Canonical Class (CppCon 2014)

```
Default constructor   T()
Destructor            ~T()
Copy constructor      T(const T&)
Copy assignment       T& operator=(const T&)
Move constructor      T(T&&) noexcept
Move assignment       T& operator=(T&&) noexcept
```

- **Rule of Zero** (preferred): if every member manages itself, declare none
  of the six; comment that it is intentional if the class looks non-trivial.
- **Rule of Five**: declare any of destructor, copy or move operations and
  declare all five.
- Moves are `noexcept` (containers rely on it); copies are not.
- Assignment: either `operator=(const T&)` plus `operator=(T&&) noexcept`,
  or one by-value copy-and-swap `operator=(T)`, never both (ambiguous).

```cpp
T(T&& other) noexcept : member_(std::move(other.member_)) {}

T& operator=(T other) {            // copy-and-swap: serves copy and move
    swap(*this, other);
    return *this;
}
friend void swap(T& a, T& b) noexcept {
    using std::swap;
    swap(a.member_, b.member_);
}

T(const T&) = delete;              // non-copyable
T& operator=(const T&) = delete;
```

## General
- Prefer `std::string` / `std::string_view` over `char *` in new code; C
  interface boundaries keep `const char *`.
- Range-based `for` where the index is not needed.
- No `using namespace std` at file scope.
