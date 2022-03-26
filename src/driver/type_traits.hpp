#pragma once

namespace std
{
	// TEMPLATE CLASS remove_reference
	template <class _Ty>
	struct remove_reference
	{
		// remove reference
		typedef _Ty type;
	};

	template <class _Ty>
	struct remove_reference<_Ty&>
	{
		// remove reference
		typedef _Ty type;
	};

	template <class _Ty>
	struct remove_reference<_Ty&&>
	{
		// remove rvalue reference
		typedef _Ty type;
	};

	template <typename T>
	typename remove_reference<T>::type&& move(T&& arg)
	{
		return static_cast<typename remove_reference<T>::type&&>(arg);
	}

	template <class _Ty>
	using remove_reference_t = typename remove_reference<_Ty>::type;

	// TEMPLATE FUNCTION forward
	template <class _Ty>
	inline
	constexpr _Ty&& forward(
		typename remove_reference<_Ty>::type& _Arg)
	{
		// forward an lvalue as either an lvalue or an rvalue
		return (static_cast<_Ty&&>(_Arg));
	}

	template <class _Ty>
	inline
	constexpr _Ty&& forward(
		typename remove_reference<_Ty>::type&& _Arg)
	{
		// forward an rvalue as an rvalue
		return (static_cast<_Ty&&>(_Arg));
	}
	
	template< class T > struct remove_cv                   { typedef T type; };
	template< class T > struct remove_cv<const T>          { typedef T type; };
	template< class T > struct remove_cv<volatile T>       { typedef T type; };
	template< class T > struct remove_cv<const volatile T> { typedef T type; };
 
	template< class T > struct remove_const                { typedef T type; };
	template< class T > struct remove_const<const T>       { typedef T type; };
 
	template< class T > struct remove_volatile             { typedef T type; };
	template< class T > struct remove_volatile<volatile T> { typedef T type; };
}
