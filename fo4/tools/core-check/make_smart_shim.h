#pragma once
namespace hdt { template <class T> RE::BSTSmartPointer<T> make_smart(T* p) { RE::BSTSmartPointer<T> r; r.reset(p); return r; } }
