// Host-only stand-ins for the three engine types FSMP's physics core uses.
// Semantics are close enough to compile and to run the core in a test, not to match the game.
#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace RE
{
	class BSFixedString
	{
	public:
		BSFixedString() = default;
		BSFixedString(const char* a) : _s(a ? a : "") {}
		BSFixedString(std::string_view a) : _s(a) {}
		const char* c_str() const noexcept { return _s.c_str(); }
		const char* data() const noexcept { return _s.c_str(); }
		bool empty() const noexcept { return _s.empty(); }
		std::size_t size() const noexcept { return _s.size(); }
		operator std::string_view() const noexcept { return _s; }
		friend bool operator==(const BSFixedString& a, const BSFixedString& b) noexcept { return a._s == b._s; }
		friend bool operator==(const BSFixedString& a, const char* b) noexcept { return a._s == (b ? b : ""); }
	private:
		std::string _s;
	};

	struct BSIntrusiveRefCounted
	{
		std::uint32_t IncRef() const { return ++_refCount; }
		std::uint32_t DecRef() const { return --_refCount; }
		mutable std::atomic<std::uint32_t> _refCount{ 0 };
	};

	template <class T>
	class BSTSmartPointer
	{
	public:
		BSTSmartPointer() = default;
		BSTSmartPointer(std::nullptr_t) {}
		explicit BSTSmartPointer(T* p) : _p(p) { if (_p) _p->IncRef(); }
		template <class U> BSTSmartPointer(const BSTSmartPointer<U>& o) : _p(o.get()) { if (_p) _p->IncRef(); }
		BSTSmartPointer(const BSTSmartPointer& o) : _p(o._p) { if (_p) _p->IncRef(); }
		BSTSmartPointer(BSTSmartPointer&& o) noexcept : _p(std::exchange(o._p, nullptr)) {}
		~BSTSmartPointer() { reset(); }
		BSTSmartPointer& operator=(const BSTSmartPointer& o) { if (this != &o) { reset(); _p = o._p; if (_p) _p->IncRef(); } return *this; }
		BSTSmartPointer& operator=(BSTSmartPointer&& o) noexcept { if (this != &o) { reset(); _p = std::exchange(o._p, nullptr); } return *this; }
		void reset(T* p = nullptr) { if (_p && _p->DecRef() == 0) delete _p; _p = p; if (_p) _p->IncRef(); }
		T* get() const noexcept { return _p; }
		T* operator->() const noexcept { return _p; }
		T& operator*() const noexcept { return *_p; }
		explicit operator bool() const noexcept { return _p != nullptr; }
		friend bool operator==(const BSTSmartPointer& a, const BSTSmartPointer& b) noexcept { return a._p == b._p; }
	private:
		T* _p{ nullptr };
	};

	template <class T, class... Args>
	BSTSmartPointer<T> make_smart(Args&&... a) { return BSTSmartPointer<T>(new T(std::forward<Args>(a)...)); }
}

template <>
struct std::hash<RE::BSFixedString>
{
	std::size_t operator()(const RE::BSFixedString& s) const noexcept { return std::hash<std::string_view>{}(s); }
};
