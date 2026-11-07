#pragma once
// Stand-ins for what fo4/src/PCH.h adds on top of CommonLibF4 (keep in step with it).
namespace RE
{
	template <class T1, class T2> bool operator==(const BSTSmartPointer<T1>& a, T2* b) { return a.get() == b; }
	template <class T1, class T2> bool operator!=(const BSTSmartPointer<T1>& a, T2* b) { return !(a.get() == b); }
}
namespace hdt { template <class T> RE::BSTSmartPointer<T> make_smart(T* p) { RE::BSTSmartPointer<T> r; r.reset(p); return r; } }
