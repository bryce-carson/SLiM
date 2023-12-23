//
//  eidos_value.h
//  Eidos
//
//  Created by Ben Haller on 4/7/15.
//  Copyright (c) 2015-2023 Philipp Messer.  All rights reserved.
//	A product of the Messer Lab, http://messerlab.org/slim/
//

//	This file is part of Eidos.
//
//	Eidos is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by
//	the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
//
//	Eidos is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License along with Eidos.  If not, see <http://www.gnu.org/licenses/>.

/*
 
 The class EidosValue represents any variable value in a EidosInterpreter context.  EidosValue itself is an abstract base class.
 Subclasses define types for NULL, logical, string, integer, and float types.  EidosValue types are generally vectors (although
 null is an exception); there are no scalar types in Eidos.
 
 */

#ifndef __Eidos__eidos_value__
#define __Eidos__eidos_value__


#include <iostream>
#include <vector>
#include <string>
#include <initializer_list>
#include <unordered_map>
#include <memory>

#include "eidos_globals.h"
#include "eidos_intrusive_ptr.h"
#include "eidos_object_pool.h"
#include "eidos_script.h"
#include "eidos_property_signature.h"
#include "eidos_call_signature.h"
#include "eidos_class_Object.h"
#include "eidos_class_Dictionary.h"
#include "json_fwd.hpp"


// EidosValues must be allocated out of the global pool, for speed.  See eidos_object_pool.h.  When Eidos disposes of an object,
// it will assume that it was allocated from this pool, so its use is mandatory except for stack-allocated objects.
extern EidosObjectPool *gEidosValuePool;


// Global EidosValues that are defined at Eidos_WarmUp() time and are never deallocated.
extern EidosValue_VOID_SP gStaticEidosValueVOID;

extern EidosValue_NULL_SP gStaticEidosValueNULL;
extern EidosValue_NULL_SP gStaticEidosValueNULLInvisible;

extern EidosValue_Logical_SP gStaticEidosValue_Logical_ZeroVec;
extern EidosValue_Int_SP gStaticEidosValue_Integer_ZeroVec;
extern EidosValue_Float_SP gStaticEidosValue_Float_ZeroVec;
extern EidosValue_String_SP gStaticEidosValue_String_ZeroVec;
extern EidosValue_Object_SP gStaticEidosValue_Object_ZeroVec;

extern EidosValue_Logical_SP gStaticEidosValue_LogicalT;
extern EidosValue_Logical_SP gStaticEidosValue_LogicalF;

extern EidosValue_Int_SP gStaticEidosValue_Integer0;
extern EidosValue_Int_SP gStaticEidosValue_Integer1;
extern EidosValue_Int_SP gStaticEidosValue_Integer2;
extern EidosValue_Int_SP gStaticEidosValue_Integer3;

extern EidosValue_Float_SP gStaticEidosValue_Float0;
extern EidosValue_Float_SP gStaticEidosValue_Float0Point5;
extern EidosValue_Float_SP gStaticEidosValue_Float1;
extern EidosValue_Float_SP gStaticEidosValue_Float10;
extern EidosValue_Float_SP gStaticEidosValue_FloatINF;
extern EidosValue_Float_SP gStaticEidosValue_FloatNAN;
extern EidosValue_Float_SP gStaticEidosValue_FloatE;
extern EidosValue_Float_SP gStaticEidosValue_FloatPI;

extern EidosValue_String_SP gStaticEidosValue_StringEmpty;
extern EidosValue_String_SP gStaticEidosValue_StringSpace;
extern EidosValue_String_SP gStaticEidosValue_StringAsterisk;
extern EidosValue_String_SP gStaticEidosValue_StringDoubleAsterisk;
extern EidosValue_String_SP gStaticEidosValue_StringComma;
extern EidosValue_String_SP gStaticEidosValue_StringPeriod;
extern EidosValue_String_SP gStaticEidosValue_StringDoubleQuote;
extern EidosValue_String_SP gStaticEidosValue_String_ECMAScript;
extern EidosValue_String_SP gStaticEidosValue_String_indices;
extern EidosValue_String_SP gStaticEidosValue_String_average;


#pragma mark -
#pragma mark Comparing EidosValues
#pragma mark -

// Comparing values is a bit complex, because we want it to be as fast as possible, but there are issues involved,
// particularly type promotion; you can compare a string to integer, or less extremely, a float to an integer, and
// the appropriate promotion of values needs to happen.  The other issue is orderability; we used to use a scheme
// here with comparison functions that returned -1/0/+1 for less-than/equal/greater-than, as is common is C++, but
// the problem is that NAN values are unorderable according to the IEEE spec; NAN < x is false, but NAN > x is also
// false, and NAN == x is also false.  So the old design was fundamentally flawed because it couldn't return the
// correct IEEE values for NAN values.  Now we provide a general-purpose comparison function that takes an operator
// enum so it can do the correct comparison, and we also provide a promotion function that tells the caller what
// type the operands in a comparison would be promoted to, so the caller can do the comparisons themselves.  The
// type-specific comparison functions are now gone.
enum class EidosComparisonOperator : uint8_t
{
	kLess = 0,
	kLessOrEqual,
	kEqual,
	kGreaterOrEqual,
	kGreater,
	kNotEqual
};

EidosValueType EidosTypeForPromotion(EidosValueType p_type1, EidosValueType p_type2, const EidosToken *p_blame_token);
bool CompareEidosValues(const EidosValue &p_value1, int p_index1, const EidosValue &p_value2, int p_index2, EidosComparisonOperator p_operator, const EidosToken *p_blame_token);


#pragma mark -
#pragma mark EidosValue
#pragma mark -

//	*********************************************************************************************************
//
//	A class representing a value resulting from script evaluation.  Eidos is quite dynamically typed;
//	problems cause runtime exceptions.  EidosValue is the abstract base class for all value classes.
//

// BCH 11/27/2017: I am converting some of the EidosValue implementations that used a std::vector internally
// to use a malloc'ed pointer instead.  This buys us several concrete speed benefits:
//
//	-	When generating a new vector value, we can allocate the EidosValue object, set its size to a known N,
//		and then set the values inside it without needing to either zero-initialize the buffer first or set
//		the count after each value added.  In contrast, std::vector does not allow this mode of operation;
//		you can either resize the vector first (which zero-initializes), or you can reserve but not resize
//		(avoiding the zero-initialize) and then emplace_back() values, but that writes a new count value out to
//		memory on every push.  Since this sort of large-N result generation is very common, this is a big win.
//
//	-	When adding new values, it lets us skip both bounds-checking and capacity-checking when we know we
//		have allocated a sufficiently big buffer to hold the push.  This is sometimes worthwhile as well;
//		sometimes the checks done by std::vector end up taking zero time due to pipelining, but sometimes not.
//
//	-	We no longer rely on the compiler being super-smart to optimize things for us.  In practice modern
//		C++ compilers *are* super-smart, but only when their optimization level is set high.  In practice,
//		this means that we see a significant speedup compared to std::vector when running an unoptimized
//		debugging build, which is a nice benefit for me, albeit with no impact for end users.


// BCH 12/22/2023: Adding "constness" as a property of EidosValue, in preparation for other work that will
// need this.  Note that the internal state of object elements is NOT const, just the EidosValue containing
// the object elements!  There is no concept of "constness" for object elements themselves.  Also, note that
// EidosSymbolTable has its own concept of "constness", in the form of tables that are considered constant;
// that is distinct from the constness of EidosValues, although there is often overlap.  Yes, this is a bit
// confusing.  We want to be able to set up things like pseudo-parameters as rapidly as possible (thus the
// concept of a "constants table"), but we also want EidosValue itself to have a concept of constness so
// that constant values like T, F, NULL, NAN, etc. are blocked from modification even if those values are
// found in a "variables table".  Basically, if *either* condition is met - living in a constants table, or
// being marked as constant in the EidosValue - modification should be blocked in all code paths.
//
// This macro checks for modification of a constant EidosValue.  It is active only in DEBUG builds, because
// it represents an internal error -- modification of a constant value should be blocked prior to this check
// in all code paths, so this is like an assert(), not the front-line defense.
#if DEBUG
#define WILL_MODIFY(x)	if ((x)->constant_) RaiseForImmutabilityCall();
#else
#define WILL_MODIFY(x)	if ((x)->constant_) RaiseForImmutabilityCall();
#endif


class EidosValue
{
	//	This class has its assignment operator disabled, to prevent accidental copying.
protected:
	
	mutable uint32_t intrusive_ref_count_;					// used by Eidos_intrusive_ptr
	
	const EidosValueType cached_type_;						// allows Type() to be an inline function; cached at construction (uint8_t)
	unsigned int constant_ : 1;								// if set, this EidosValue is a constant and cannot be modified
	unsigned int invisible_ : 1;							// as in R; if true, the value will not normally be printed to the console
	unsigned int is_singleton_ : 1;							// allows Count() and IsSingleton() to be inline; cached at construction
	unsigned int registered_for_patching_ : 1;				// used by EidosValue_Object, otherwise UNINITIALIZED; declared here for reasons of memory packing
	
	int64_t *dim_;											// nullptr for vectors; points to a malloced, OWNED array of dimensions for matrices and arrays
															//    when allocated, the first value in the buffer is a count of the dimensions that follow
	virtual void _CopyDimensionsFromValue(const EidosValue *p_value);											// do not call directly; called by CopyDimensionsFromValue()
	void PrintMatrixFromIndex(int64_t p_ncol, int64_t p_nrow, int64_t p_start_index, std::ostream &p_ostream, const std::string &p_indent = std::string()) const;
	
	virtual int Count_Virtual(void) const = 0;				// the number of values in the vector
	
public:
	
	EidosValue(const EidosValue &p_original) = delete;		// no copy-construct
	EidosValue& operator=(const EidosValue&) = delete;		// no copying
	
	EidosValue(void) = delete;									// no null constructor
	EidosValue(EidosValueType p_value_type, bool p_singleton);	// must construct with a type identifier and singleton flag, which will be cached
	virtual ~EidosValue(void);
	
	// methods that raise due to various causes, used to avoid duplication and allow efficient inlining
	void RaiseForIncorrectTypeCall(void) const __attribute__((__noreturn__)) __attribute__((analyzer_noreturn));
	void RaiseForImmutabilityCall(void) const __attribute__((__noreturn__)) __attribute__((analyzer_noreturn));
	void RaiseForCapacityViolation(void) const __attribute__((__noreturn__)) __attribute__((analyzer_noreturn));
	void RaiseForRangeViolation(void) const __attribute__((__noreturn__)) __attribute__((analyzer_noreturn));
	void RaiseForRetainReleaseViolation(void) const __attribute__((__noreturn__)) __attribute__((analyzer_noreturn));
	
	// basic methods
	inline __attribute__((always_inline)) EidosValueType Type(void) const { return cached_type_; }	// the type of the vector, cached at construction
	inline __attribute__((always_inline)) bool IsSingleton(void) const { return is_singleton_; }	// true is the subclass is a singleton subclass (not just if Count()==1)
	inline __attribute__((always_inline)) int Count(void) const { return (is_singleton_ ? 1 : Count_Virtual()); }	// avoid the virtual function call for singletons
	
	// constness; note that the internal state of object elements is NOT const, just the EidosValue containing the object elements
	inline __attribute__((always_inline)) void MarkAsConstant(void) { constant_ = true; }
	inline __attribute__((always_inline)) bool IsConstant(void) const { return constant_; }
	
	virtual const std::string &ElementType(void) const = 0;	// the type of the elements contained by the vector
	void Print(std::ostream &p_ostream, const std::string &p_indent = std::string()) const;				// standard printing; same as operator<<
	void PrintStructure(std::ostream &p_ostream, int max_values) const;	// print structure; no newline
	virtual void PrintValueAtIndex(const int p_idx, std::ostream &p_ostream) const = 0;
	virtual nlohmann::json JSONRepresentation(void) const = 0;
	
	// object invisibility; note invisibility should only be changed on uniquely owned objects, to avoid side effects
	inline __attribute__((always_inline)) bool Invisible(void) const							{ return invisible_; }
	inline __attribute__((always_inline)) void SetInvisible(bool p_invisible)					{ WILL_MODIFY(this); invisible_ = p_invisible; }
	
	// basic subscript access; abstract here since we want to force subclasses to define this
	virtual EidosValue_SP GetValueAtIndex(const int p_idx, const EidosToken *p_blame_token) const = 0;
	
	// fetching individual values WITHOUT casting; the base class behavior is to raise if the type does not match
	// these are convenience accessors; to get values across a large vector, the XData() methods below are preferred
	// note that NumericAtIndex_NOCAST() accesses "numeric" values (integer or float), casting them to float,
	// so it is technically a casting method, but "numeric" is privileged and is not considered full casting
	virtual eidos_logical_t LogicalAtIndex_NOCAST(__attribute__((unused)) int p_idx, __attribute__((unused)) const EidosToken *p_blame_token) const { RaiseForIncorrectTypeCall(); }
	virtual std::string StringAtIndex_NOCAST(__attribute__((unused)) int p_idx, __attribute__((unused)) const EidosToken *p_blame_token) const { RaiseForIncorrectTypeCall(); }
	virtual int64_t IntAtIndex_NOCAST(__attribute__((unused)) int p_idx, __attribute__((unused)) const EidosToken *p_blame_token) const { RaiseForIncorrectTypeCall(); }
	virtual double FloatAtIndex_NOCAST(__attribute__((unused)) int p_idx, __attribute__((unused)) const EidosToken *p_blame_token) const { RaiseForIncorrectTypeCall(); }
	virtual double NumericAtIndex_NOCAST(__attribute__((unused)) int p_idx, __attribute__((unused)) const EidosToken *p_blame_token) const { RaiseForIncorrectTypeCall(); }	// casts integer to float, otherwise does not cast; considered _NOCAST
	virtual EidosObject *ObjectElementAtIndex_NOCAST(__attribute__((unused)) int p_idx, __attribute__((unused)) const EidosToken *p_blame_token) const { RaiseForIncorrectTypeCall(); }
	
	// fetching individual values WITH a cast to the requested type; this is not general-purpose
	// behavior for Eidos, but is limited to specific places in the language:
	//
	//	 -- CompareEidosValues(), which is now used only by pmax()/pmin() and only for identical types
	//	 -- _AssignRValueToLValue() to put a value of one type into a specific index in an existing vector that might be a different type
	//	 -- string + in EvaluatePlus(), which coerces everything that isn't a string into a string, and cat() / catn() / paste() / paste0() / print()
	//	 -- Evaluate_Conditional() / Evaluate_If() / Evaluate_Do() / Evaluate_While() to cast their condition to logical
	//	 -- Evaluate_And() / Evaluate_Or() / Evaluate_Not() to cast values used with operators & | ^ to logical
	//	 -- Evaluate_Eq() and the other comparison operators, == < <= > >= !=, which compare on the basis of promoting up to a common type
	//	 -- ConcatenateEidosValues() for c() / apply() / sapply() / AppendKeysAndValuesFrom(), and property accesses / method calls
	//	 -- the asLogical() / asInteger() / asFloat() / asString() methods, which explicitly coerce one type into another
	//
	virtual eidos_logical_t LogicalAtIndex_CAST(int p_idx, const EidosToken *p_blame_token) const;
	virtual std::string StringAtIndex_CAST(int p_idx, const EidosToken *p_blame_token) const;
	virtual int64_t IntAtIndex_CAST(int p_idx, const EidosToken *p_blame_token) const;
	virtual double FloatAtIndex_CAST(int p_idx, const EidosToken *p_blame_token) const;
	virtual EidosObject *ObjectElementAtIndex_CAST(int p_idx, const EidosToken *p_blame_token) const;
	
	// methods to allow type-agnostic manipulation of EidosValues
	virtual EidosValue_SP VectorBasedCopy(void) const;				// just calls CopyValues() by default, but guarantees a mutable copy
	virtual EidosValue_SP CopyValues(void) const = 0;				// a deep copy of the receiver with invisible_ == false
	virtual EidosValue_SP NewMatchingType(void) const = 0;			// a new EidosValue instance of the same type as the receiver
	virtual void PushValueFromIndexOfEidosValue(int p_idx, const EidosValue &p_source_script_value, const EidosToken *p_blame_token) = 0;	// copy a value
	virtual void Sort(bool p_ascending) = 0;
	
	// Methods to get a type-specific pointer directly to the data of the EidosValue.  This is generally good for either
	// accessing the values without changing them, or changing them but not changing the length of the vector.  You must
	// explicitly request mutability.  If you want to change the length of the vector, you will want to actually get the
	// type-specific vector subclass using dynamic_cast, but it is rare not to already have that pointer in such cases.
	virtual const eidos_logical_t *LogicalData(void) const { RaiseForIncorrectTypeCall(); }
	virtual eidos_logical_t *LogicalData_Mutable(void) { RaiseForIncorrectTypeCall(); }
	virtual const std::string *StringData(void) const { RaiseForIncorrectTypeCall(); }
	virtual std::string *StringData_Mutable(void) { RaiseForIncorrectTypeCall(); }
	virtual const int64_t *IntData(void) const { RaiseForIncorrectTypeCall(); }
	virtual int64_t *IntData_Mutable(void) { RaiseForIncorrectTypeCall(); }
	virtual const double *FloatData(void) const { RaiseForIncorrectTypeCall(); }
	virtual double *FloatData_Mutable(void) { RaiseForIncorrectTypeCall(); }
	virtual EidosObject * const *ObjectData(void) const { RaiseForIncorrectTypeCall(); }
	virtual EidosObject **ObjectData_Mutable(void) { RaiseForIncorrectTypeCall(); }
	
	// Dimension support, for matrices and arrays
	inline __attribute__((always_inline)) bool IsMatrixOrArray(void) const { return !!dim_; }					// true if we have a dimensions buffer
	inline __attribute__((always_inline)) int DimensionCount(void) const { return (!dim_) ? 1 : (int)*dim_; }	// 1 for vectors, 2 for matrices, 2...n for arrays (1 not allowed for arrays)
	inline __attribute__((always_inline)) const int64_t *Dimensions(void) const { return (!dim_) ? nullptr : dim_ + 1; }	// nullptr or a pointer into the dim_ buffer
	
	inline __attribute__((always_inline)) EidosValue *CopyDimensionsFromValue(const EidosValue *p_value)		// copy dimensions from another value; checks for validity, can raise
	{
		if ((p_value && p_value->dim_) || dim_)
			_CopyDimensionsFromValue(p_value);
		return this;
	}
	
	void SetDimensions(int64_t p_dim_count, const int64_t *p_dim_buffer);										// can be 1/nullptr to make the value a vector; 0 is not allowed
	
	static bool MatchingDimensions(const EidosValue *p_value1, const EidosValue *p_value2);						// true if dimensionalities are identical; works for vectors as well as matrices/arrays
	static inline __attribute__((always_inline)) EidosValue *BinaryOperationDimensionSource(EidosValue *p_value1, EidosValue *p_value2)	// chooses which value's dimensionality will carry through a binary op
	{
		int dim1 = p_value1->DimensionCount();
		int dim2 = p_value2->DimensionCount();
		
		if ((dim1 == 1) && (dim2 == 1))
			return nullptr;							// neither is a matrix/array, so it doesn't matter; avoid the EidosValue_SP overhead
		
		int count1 = p_value1->Count();
		int count2 = p_value2->Count();
		
		if (dim2 == 1)
		{
			// p_value1 is a matrix/array, p_value2 is not; prefer the matrix, except that we won't promote a non-singleton vector to be a matrix
			if ((count1 == 1) && (count2 != 1))
				return p_value2;
			return p_value1;
		}
		else if (dim1 == 1)
		{
			// p_value2 is a matrix/array, p_value1 is not; prefer the matrix, except that we won't promote a non-singleton vector to be a matrix
			if ((count2 == 1) && (count1 != 1))
				return p_value1;
			return p_value2;
		}
		else
		{
			return p_value1;							// both are a matrix/array, so it doesn't matter (either they are conformable, or there's an error anyway)
		}
	}
	
	EidosValue_SP Subset(std::vector<std::vector<int64_t>> &p_inclusion_indices, bool p_drop, const EidosToken *p_blame_token);
	
	// Eidos_intrusive_ptr support; we use Eidos_intrusive_ptr as a fast smart pointer to EidosValue.
	inline __attribute__((always_inline)) uint32_t UseCount() const { return intrusive_ref_count_; }
	inline __attribute__((always_inline)) void StackAllocated() { intrusive_ref_count_++; }			// used with stack-allocated EidosValues that have to be put under Eidos_intrusive_ptr
	
	friend void Eidos_intrusive_ptr_add_ref(const EidosValue *p_value);
	friend void Eidos_intrusive_ptr_release(const EidosValue *p_value);
	
	// This is a hack scheme to track EidosValue allocations and deallocations, as a way to help debug leaks
	// To enable it, change the #undef to #define.  When you run Eidos tests, a summary of persistent EidosValues will print.
	// The standard global values should be persistent (T, F, INF, NAN, NULL, NULL-invisible, E, PI); all others should be freed.
#undef EIDOS_TRACK_VALUE_ALLOCATION
	
#ifdef EIDOS_TRACK_VALUE_ALLOCATION
	static int valueTrackingCount;
	static std::vector<EidosValue *> valueTrackingVector;
#endif
};

std::ostream &operator<<(std::ostream &p_outstream, const EidosValue &p_value);

// Eidos_intrusive_ptr support
inline __attribute__((always_inline)) void Eidos_intrusive_ptr_add_ref(const EidosValue *p_value)
{
	THREAD_SAFETY_IN_ACTIVE_PARALLEL("Eidos_intrusive_ptr_add_ref(): EidosValue intrusive_ref_count_ change");
	
	++(p_value->intrusive_ref_count_);
}

inline __attribute__((always_inline)) void Eidos_intrusive_ptr_release(const EidosValue *p_value)
{
	THREAD_SAFETY_IN_ACTIVE_PARALLEL("Eidos_intrusive_ptr_release(): EidosValue intrusive_ref_count_ change");
	
	if ((--(p_value->intrusive_ref_count_)) == 0)
	{
		// We no longer delete; all EidosValues under Eidos_intrusive_ptr should have been allocated out of gEidosValuePool, so it handles the free
		//delete p_value;
		p_value->~EidosValue();
		gEidosValuePool->DisposeChunk(const_cast<EidosValue*>(p_value));
	}
}


#pragma mark -
#pragma mark EidosValue_VOID
#pragma mark -

//	*********************************************************************************************************
//
//	EidosValue_VOID represents void values in Eidos.  We have one static global EidosValue_VOID instance,
//	which should be used for all void values.
//

class EidosValue_VOID final : public EidosValue
{
private:
	typedef EidosValue super;
	
protected:
	virtual int Count_Virtual(void) const override { return 0; }
	
public:
	EidosValue_VOID(const EidosValue_VOID &p_original) = delete;	// no copy-construct
	EidosValue_VOID& operator=(const EidosValue_VOID&) = delete;	// no copying
	
	inline EidosValue_VOID(void) : EidosValue(EidosValueType::kValueVOID, false) { }
	inline virtual ~EidosValue_VOID(void) override { }
	
	virtual const std::string &ElementType(void) const override;
	virtual void PrintValueAtIndex(const int p_idx, std::ostream &p_ostream) const override;
	virtual nlohmann::json JSONRepresentation(void) const override;
	
	virtual EidosValue_SP GetValueAtIndex(const int p_idx, const EidosToken *p_blame_token) const override;
	
	virtual EidosValue_SP CopyValues(void) const override;
	virtual EidosValue_SP NewMatchingType(void) const override;
	virtual void PushValueFromIndexOfEidosValue(int p_idx, const EidosValue &p_source_script_value, const EidosToken *p_blame_token) override;
	virtual void Sort(bool p_ascending) override;
};


#pragma mark -
#pragma mark EidosValue_NULL
#pragma mark -

//	*********************************************************************************************************
//
//	EidosValue_NULL represents NULL values in Eidos.  We have two static global EidosValue_NULL instances,
//	representing invisible versus non-invisible NULL.
//

class EidosValue_NULL final : public EidosValue
{
private:
	typedef EidosValue super;

protected:
	virtual int Count_Virtual(void) const override { return 0; }
	
public:
	EidosValue_NULL(const EidosValue_NULL &p_original) = delete;	// no copy-construct
	EidosValue_NULL& operator=(const EidosValue_NULL&) = delete;	// no copying
	
	inline EidosValue_NULL(void) : EidosValue(EidosValueType::kValueNULL, false) { }
	inline virtual ~EidosValue_NULL(void) override { }
	
	virtual const std::string &ElementType(void) const override;
	virtual void PrintValueAtIndex(const int p_idx, std::ostream &p_ostream) const override;
	virtual nlohmann::json JSONRepresentation(void) const override;
	
	virtual EidosValue_SP GetValueAtIndex(const int p_idx, const EidosToken *p_blame_token) const override;

	virtual EidosValue_SP CopyValues(void) const override;
	virtual EidosValue_SP NewMatchingType(void) const override;
	virtual void PushValueFromIndexOfEidosValue(int p_idx, const EidosValue &p_source_script_value, const EidosToken *p_blame_token) override;
	virtual void Sort(bool p_ascending) override;
};


#pragma mark -
#pragma mark EidosValue_Logical
#pragma mark -

//	*********************************************************************************************************
//
//	EidosValue_Logical represents logical (bool) values in Eidos.  Unlike other EidosValue types, for
//	logical the EidosValue_Logical class itself is a non-abstract class that models a vector of logical
//	values; there is no singleton version.  This is because there are only two possible singleton values,
//	T and F, represented by the global constants gStaticEidosValue_LogicalT and gStaticEidosValue_LogicalF;
//	those should be used for singleton values when possible.
//

class EidosValue_Logical : public EidosValue
{
private:
	typedef EidosValue super;

protected:
	eidos_logical_t *values_ = nullptr;
	size_t count_ = 0, capacity_ = 0;
	
protected:
	// protected to encourage use of gStaticEidosValue_LogicalT / gStaticEidosValue_LogicalF
	explicit EidosValue_Logical(eidos_logical_t p_logical1);
	
	virtual int Count_Virtual(void) const override { return (int)count_; }
	
public:
	EidosValue_Logical(const EidosValue_Logical &p_original) = delete;	// no copy-construct
	EidosValue_Logical& operator=(const EidosValue_Logical&) = delete;	// no copying
	
	inline EidosValue_Logical(void) : EidosValue(EidosValueType::kValueLogical, false) { }
	explicit EidosValue_Logical(const std::vector<eidos_logical_t> &p_logicalvec);
	explicit EidosValue_Logical(std::initializer_list<eidos_logical_t> p_init_list);
	explicit EidosValue_Logical(const eidos_logical_t *p_values, size_t p_count);
	inline virtual ~EidosValue_Logical(void) override { free(values_); }
	
	virtual const std::string &ElementType(void) const override;
	virtual void PrintValueAtIndex(const int p_idx, std::ostream &p_ostream) const override;
	virtual nlohmann::json JSONRepresentation(void) const override;
	
	virtual const eidos_logical_t *LogicalData(void) const override { return values_; }
	virtual eidos_logical_t *LogicalData_Mutable(void) override { WILL_MODIFY(this); return values_; }
	
	virtual eidos_logical_t LogicalAtIndex_NOCAST(int p_idx, const EidosToken *p_blame_token) const override;
	
	virtual eidos_logical_t LogicalAtIndex_CAST(int p_idx, const EidosToken *p_blame_token) const override;
	virtual std::string StringAtIndex_CAST(int p_idx, const EidosToken *p_blame_token) const override;
	virtual int64_t IntAtIndex_CAST(int p_idx, const EidosToken *p_blame_token) const override;
	virtual double FloatAtIndex_CAST(int p_idx, const EidosToken *p_blame_token) const override;
	
	virtual EidosValue_SP GetValueAtIndex(const int p_idx, const EidosToken *p_blame_token) const override;
	
	virtual EidosValue_SP CopyValues(void) const override;
	virtual EidosValue_SP NewMatchingType(void) const override;
	virtual void PushValueFromIndexOfEidosValue(int p_idx, const EidosValue &p_source_script_value, const EidosToken *p_blame_token) override;
	virtual void Sort(bool p_ascending) override;
	
	// vector lookalike methods; not virtual, only for clients with a EidosValue_Logical*
	EidosValue_Logical *reserve(size_t p_reserved_size);				// as in std::vector
	EidosValue_Logical *resize_no_initialize(size_t p_new_size);		// does not zero-initialize, unlike std::vector!
	void expand(void);													// expand to fit (at least) one new value
	void erase_index(size_t p_index);									// a weak substitute for erase()
	
	inline __attribute__((always_inline)) eidos_logical_t *data(void) { WILL_MODIFY(this); return values_; }
	inline __attribute__((always_inline)) const eidos_logical_t *data(void) const { return values_; }
	inline __attribute__((always_inline)) void push_logical(eidos_logical_t p_logical)
	{
		WILL_MODIFY(this);
		if (count_ == capacity_) expand();
		values_[count_++] = p_logical;
	}
	inline __attribute__((always_inline)) void push_logical_no_check(eidos_logical_t p_logical) {
		WILL_MODIFY(this);
#if DEBUG
		// do checks only in DEBUG mode, for speed; the user should never be able to trigger these errors
		if (count_ == capacity_) RaiseForCapacityViolation();
#endif
		values_[count_++] = p_logical;
	}
	inline __attribute__((always_inline)) void set_logical_no_check(eidos_logical_t p_logical, size_t p_index) {
		WILL_MODIFY(this);
#if DEBUG
		// do checks only in DEBUG mode, for speed; the user should never be able to trigger these errors
		if (p_index >= count_) RaiseForRangeViolation();
#endif
		values_[p_index] = p_logical;
	}
};


#pragma mark -
#pragma mark EidosValue_String
#pragma mark -

//	*********************************************************************************************************
//
//	EidosValue_String represents string (C++ std::string) values in Eidos.  The subclass
//	EidosValue_String_vector is the standard instance class, used to hold vectors of strings.
//	EidosValue_String_singleton is used for speed, to represent single values.
//

class EidosValue_String : public EidosValue
{
private:
	typedef EidosValue super;

protected:
	explicit inline EidosValue_String(bool p_singleton) : EidosValue(EidosValueType::kValueString, p_singleton) {}
	
	virtual int Count_Virtual(void) const override = 0;
	
public:
	EidosValue_String(const EidosValue_String &p_original) = delete;		// no copy-construct
	EidosValue_String(void) = delete;										// no default constructor
	EidosValue_String& operator=(const EidosValue_String&) = delete;		// no copying
	inline virtual ~EidosValue_String(void) override { }
	
	virtual const std::string &ElementType(void) const override;
	virtual void PrintValueAtIndex(const int p_idx, std::ostream &p_ostream) const override;
	virtual nlohmann::json JSONRepresentation(void) const override;
	
	virtual std::string StringAtIndex_NOCAST(int p_idx, const EidosToken *p_blame_token) const override = 0;
	virtual const std::string &StringRefAtIndex_NOCAST(int p_idx, const EidosToken *p_blame_token) const = 0;		// const reference for speed
	virtual EidosValue_SP GetValueAtIndex(const int p_idx, const EidosToken *p_blame_token) const override = 0;
	
	virtual EidosValue_SP CopyValues(void) const override = 0;
	virtual EidosValue_SP NewMatchingType(void) const override;
	virtual void PushValueFromIndexOfEidosValue(int p_idx, const EidosValue &p_source_script_value, const EidosToken *p_blame_token) override = 0;
	virtual void Sort(bool p_ascending) override = 0;
};

class EidosValue_String_vector final : public EidosValue_String
{
private:
	typedef EidosValue_String super;

protected:
	// this is not converted to a malloced buffer because unlike the other types, we can't get away with
	// not initializing the memory belonging to a std::string, so the malloc strategy doesn't work
	std::vector<std::string> values_;
	
	virtual int Count_Virtual(void) const override { return (int)values_.size(); }
	
public:
	EidosValue_String_vector(const EidosValue_String_vector &p_original) = delete;	// no copy-construct
	EidosValue_String_vector& operator=(const EidosValue_String_vector&) = delete;	// no copying
	
	inline EidosValue_String_vector(void) : EidosValue_String(false) { }
	explicit EidosValue_String_vector(const std::vector<std::string> &p_stringvec);
	EidosValue_String_vector(double *p_doublebuf, int p_buffer_length);
	//explicit EidosValue_String_vector(const std::string &p_string1);		// disabled to encourage use of EidosValue_String_singleton for this case
	explicit EidosValue_String_vector(std::initializer_list<const std::string> p_init_list);
	explicit EidosValue_String_vector(std::initializer_list<const char *> p_init_list);
	inline virtual ~EidosValue_String_vector(void) override { }
	
	virtual const std::string *StringData(void) const override { return values_.data(); }
	virtual std::string *StringData_Mutable(void) override { WILL_MODIFY(this); return values_.data(); }
	std::vector<std::string> &StringVectorData(void) { WILL_MODIFY(this); return values_; }	// to get the std::vector for direct modification
	
	inline __attribute__((always_inline)) void PushString(const std::string &p_string) { WILL_MODIFY(this); values_.emplace_back(p_string); }
	inline __attribute__((always_inline)) EidosValue_String_vector *Reserve(int p_reserved_size) { WILL_MODIFY(this); values_.reserve(p_reserved_size); return this; }
	
	virtual std::string StringAtIndex_NOCAST(int p_idx, const EidosToken *p_blame_token) const override;
	virtual const std::string &StringRefAtIndex_NOCAST(int p_idx, const EidosToken *p_blame_token) const override;
	
	virtual eidos_logical_t LogicalAtIndex_CAST(int p_idx, const EidosToken *p_blame_token) const override;
	virtual std::string StringAtIndex_CAST(int p_idx, const EidosToken *p_blame_token) const override;
	virtual int64_t IntAtIndex_CAST(int p_idx, const EidosToken *p_blame_token) const override;
	virtual double FloatAtIndex_CAST(int p_idx, const EidosToken *p_blame_token) const override;
	
	virtual EidosValue_SP GetValueAtIndex(const int p_idx, const EidosToken *p_blame_token) const override;
	
	virtual EidosValue_SP CopyValues(void) const override;
	virtual void PushValueFromIndexOfEidosValue(int p_idx, const EidosValue &p_source_script_value, const EidosToken *p_blame_token) override;
	virtual void Sort(bool p_ascending) override;
};

class EidosValue_String_singleton final : public EidosValue_String
{
private:
	typedef EidosValue_String super;

protected:
	std::string value_;
	EidosScript *cached_script_ = nullptr;	// cached by executeLambda(), apply(), and sapply() to avoid multiple tokenize/parse overhead
	
	virtual int Count_Virtual(void) const override { return 1; }
	
public:
	EidosValue_String_singleton(const EidosValue_String_singleton &p_original) = delete;	// no copy-construct
	EidosValue_String_singleton& operator=(const EidosValue_String_singleton&) = delete;	// no copying
	EidosValue_String_singleton(void) = delete;
	explicit inline EidosValue_String_singleton(const std::string &p_string1) : EidosValue_String(true), value_(p_string1) { }
	inline virtual ~EidosValue_String_singleton(void) override { delete cached_script_; }
	
	inline __attribute__((always_inline)) std::string &StringValue_Mutable(void) { WILL_MODIFY(this); delete cached_script_; cached_script_ = nullptr; return value_; }			// very dangerous; do not use
	
	virtual const std::string *StringData(void) const override { return &value_; }
	virtual std::string *StringData_Mutable(void) override { WILL_MODIFY(this); delete cached_script_; cached_script_ = nullptr; return &value_; }			// very dangerous; do not use
	inline __attribute__((always_inline)) void SetValue(const std::string &p_string) { WILL_MODIFY(this); delete cached_script_; cached_script_ = nullptr; value_ = p_string; }	// very dangerous; used only in Evaluate_For()
	
	virtual std::string StringAtIndex_NOCAST(int p_idx, const EidosToken *p_blame_token) const override;
	virtual const std::string &StringRefAtIndex_NOCAST(int p_idx, const EidosToken *p_blame_token) const override;
	
	virtual eidos_logical_t LogicalAtIndex_CAST(int p_idx, const EidosToken *p_blame_token) const override;
	virtual std::string StringAtIndex_CAST(int p_idx, const EidosToken *p_blame_token) const override;
	virtual int64_t IntAtIndex_CAST(int p_idx, const EidosToken *p_blame_token) const override;
	virtual double FloatAtIndex_CAST(int p_idx, const EidosToken *p_blame_token) const override;
	
	virtual EidosValue_SP GetValueAtIndex(const int p_idx, const EidosToken *p_blame_token) const override;
	virtual EidosValue_SP CopyValues(void) const override;
	
	virtual EidosValue_SP VectorBasedCopy(void) const override;
	
	// prohibited actions because there is no backing vector
	virtual void PushValueFromIndexOfEidosValue(int p_idx, const EidosValue &p_source_script_value, const EidosToken *p_blame_token) override;
	virtual void Sort(bool p_ascending) override;
	
	// script caching; this is something that only EidosValue_String_singleton does!
	inline __attribute__((always_inline)) EidosScript *CachedScript(void) { return cached_script_; }
	inline __attribute__((always_inline)) void SetCachedScript(EidosScript *p_script) { cached_script_ = p_script; }
};


#pragma mark -
#pragma mark EidosValue_Int
#pragma mark -

//	*********************************************************************************************************
//
//	EidosValue_Int represents integer (C++ int64_t) values in Eidos.  The subclass
//	EidosValue_Int_vector is the standard instance class, used to hold vectors of floats.
//	EidosValue_Int_singleton is used for speed, to represent single values.
//

class EidosValue_Int : public EidosValue
{
private:
	typedef EidosValue super;

protected:
	explicit inline EidosValue_Int(bool p_singleton) : EidosValue(EidosValueType::kValueInt, p_singleton) {}
	
	virtual int Count_Virtual(void) const override = 0;
	
public:
	EidosValue_Int(const EidosValue_Int &p_original) = delete;			// no copy-construct
	EidosValue_Int(void) = delete;										// no default constructor
	EidosValue_Int& operator=(const EidosValue_Int&) = delete;			// no copying
	inline virtual ~EidosValue_Int(void) override { }
	
	virtual const std::string &ElementType(void) const override;
	virtual void PrintValueAtIndex(const int p_idx, std::ostream &p_ostream) const override;
	virtual nlohmann::json JSONRepresentation(void) const override;
	
	virtual int64_t IntAtIndex_NOCAST(int p_idx, const EidosToken *p_blame_token) const override = 0;
	virtual EidosValue_SP GetValueAtIndex(const int p_idx, const EidosToken *p_blame_token) const override = 0;
	
	virtual EidosValue_SP CopyValues(void) const override = 0;
	virtual EidosValue_SP NewMatchingType(void) const override;
	virtual void PushValueFromIndexOfEidosValue(int p_idx, const EidosValue &p_source_script_value, const EidosToken *p_blame_token) override = 0;
	virtual void Sort(bool p_ascending) override = 0;
};

class EidosValue_Int_vector final : public EidosValue_Int
{
private:
	typedef EidosValue_Int super;

protected:
	int64_t *values_ = nullptr;
	size_t count_ = 0, capacity_ = 0;
	
	virtual int Count_Virtual(void) const override { return (int)count_; }
	
public:
	EidosValue_Int_vector(const EidosValue_Int_vector &p_original) = delete;	// no copy-construct
	EidosValue_Int_vector& operator=(const EidosValue_Int_vector&) = delete;	// no copying
	
	inline EidosValue_Int_vector(void) : EidosValue_Int(false) { }
	explicit EidosValue_Int_vector(const std::vector<int16_t> &p_intvec);
	explicit EidosValue_Int_vector(const std::vector<int32_t> &p_intvec);
	explicit EidosValue_Int_vector(const std::vector<int64_t> &p_intvec);
	//explicit EidosValue_Int_vector(int64_t p_int1);		// disabled to encourage use of EidosValue_Int_singleton for this case
	explicit EidosValue_Int_vector(std::initializer_list<int64_t> p_init_list);
	explicit EidosValue_Int_vector(const int64_t *p_values, size_t p_count);
	inline virtual ~EidosValue_Int_vector(void) override { free(values_); }
	
	virtual const int64_t *IntData(void) const override { return values_; }
	virtual int64_t *IntData_Mutable(void) override { WILL_MODIFY(this); return values_; }
	
	virtual int64_t IntAtIndex_NOCAST(int p_idx, const EidosToken *p_blame_token) const override;
	virtual double NumericAtIndex_NOCAST(int p_idx, const EidosToken *p_blame_token) const override;	// casts integer to float, otherwise does not cast
	
	virtual eidos_logical_t LogicalAtIndex_CAST(int p_idx, const EidosToken *p_blame_token) const override;
	virtual std::string StringAtIndex_CAST(int p_idx, const EidosToken *p_blame_token) const override;
	virtual int64_t IntAtIndex_CAST(int p_idx, const EidosToken *p_blame_token) const override;
	virtual double FloatAtIndex_CAST(int p_idx, const EidosToken *p_blame_token) const override;
	
	virtual EidosValue_SP GetValueAtIndex(const int p_idx, const EidosToken *p_blame_token) const override;
	
	virtual EidosValue_SP CopyValues(void) const override;
	virtual void PushValueFromIndexOfEidosValue(int p_idx, const EidosValue &p_source_script_value, const EidosToken *p_blame_token) override;
	virtual void Sort(bool p_ascending) override;
	
	// vector lookalike methods; not virtual, only for clients with a EidosValue_Int_vector*
	EidosValue_Int_vector *reserve(size_t p_reserved_size);				// as in std::vector
	EidosValue_Int_vector *resize_no_initialize(size_t p_new_size);		// does not zero-initialize, unlike std::vector!
	void expand(void);													// expand to fit (at least) one new value
	void erase_index(size_t p_index);									// a weak substitute for erase()
	
	inline __attribute__((always_inline)) int64_t *data(void) { WILL_MODIFY(this); return values_; }
	inline __attribute__((always_inline)) const int64_t *data(void) const { return values_; }
	inline __attribute__((always_inline)) void push_int(int64_t p_int)
	{
		WILL_MODIFY(this);
		if (count_ == capacity_) expand();
		values_[count_++] = p_int;
	}
	inline __attribute__((always_inline)) void push_int_no_check(int64_t p_int) {
		WILL_MODIFY(this);
#if DEBUG
		// do checks only in DEBUG mode, for speed; the user should never be able to trigger these errors
		if (count_ == capacity_) RaiseForCapacityViolation();
#endif
		values_[count_++] = p_int;
	}
	inline __attribute__((always_inline)) void set_int_no_check(int64_t p_int, size_t p_index) {
		WILL_MODIFY(this);
#if DEBUG
		// do checks only in DEBUG mode, for speed; the user should never be able to trigger these errors
		if (p_index >= count_) RaiseForRangeViolation();
#endif
		values_[p_index] = p_int;
	}
};

class EidosValue_Int_singleton final : public EidosValue_Int
{
private:
	typedef EidosValue_Int super;

protected:
	int64_t value_;
	
	virtual int Count_Virtual(void) const override { return 1; }
	
public:
	EidosValue_Int_singleton(const EidosValue_Int_singleton &p_original) = delete;	// no copy-construct
	EidosValue_Int_singleton& operator=(const EidosValue_Int_singleton&) = delete;	// no copying
	EidosValue_Int_singleton(void) = delete;
	explicit inline EidosValue_Int_singleton(int64_t p_int1) : EidosValue_Int(true), value_(p_int1) { }
	inline virtual ~EidosValue_Int_singleton(void) override { }
	
	virtual const int64_t *IntData(void) const override { return &value_; }
	virtual int64_t *IntData_Mutable(void) override { WILL_MODIFY(this); return &value_; }	// very dangerous; used only in Evaluate_Assign()
	inline __attribute__((always_inline)) void SetValue(int64_t p_int) { WILL_MODIFY(this); value_ = p_int; }		// very dangerous; used only in Evaluate_For()
	
	virtual int64_t IntAtIndex_NOCAST(int p_idx, const EidosToken *p_blame_token) const override;
	virtual double NumericAtIndex_NOCAST(int p_idx, const EidosToken *p_blame_token) const override;	// casts integer to float, otherwise does not cast
	
	virtual eidos_logical_t LogicalAtIndex_CAST(int p_idx, const EidosToken *p_blame_token) const override;
	virtual std::string StringAtIndex_CAST(int p_idx, const EidosToken *p_blame_token) const override;
	virtual int64_t IntAtIndex_CAST(int p_idx, const EidosToken *p_blame_token) const override;
	virtual double FloatAtIndex_CAST(int p_idx, const EidosToken *p_blame_token) const override;
	
	virtual EidosValue_SP GetValueAtIndex(const int p_idx, const EidosToken *p_blame_token) const override;
	virtual EidosValue_SP CopyValues(void) const override;
	
	virtual EidosValue_SP VectorBasedCopy(void) const override;
	
	// prohibited actions because there is no backing vector
	virtual void PushValueFromIndexOfEidosValue(int p_idx, const EidosValue &p_source_script_value, const EidosToken *p_blame_token) override;
	virtual void Sort(bool p_ascending) override;
};


#pragma mark -
#pragma mark EidosValue_Float
#pragma mark -

//	*********************************************************************************************************
//
//	EidosValue_Float represents floating-point (C++ double) values in Eidos.
//

class EidosValue_Float : public EidosValue
{
private:
	typedef EidosValue super;

protected:
	// singleton/vector design: values_ will either point to singleton_value_, or to a malloced buffer; it will never be nullptr
	// in the case of a zero-length vector, note that values_ will point to singleton_value_ with count_ == 0 but capacity_ == 1
	double singleton_value_;
	double *values_;
	size_t count_, capacity_;
	
	virtual int Count_Virtual(void) const override { return (int)count_; }
	
public:
	EidosValue_Float(const EidosValue_Float &p_original) = delete;			// no copy-construct
	EidosValue_Float& operator=(const EidosValue_Float&) = delete;			// no copying
	
	explicit inline EidosValue_Float(void) : EidosValue(EidosValueType::kValueFloat, false), values_(&singleton_value_), count_(0), capacity_(1) { }
	explicit inline EidosValue_Float(double p_float1) : EidosValue(EidosValueType::kValueFloat, false), singleton_value_(p_float1), values_(&singleton_value_), count_(1), capacity_(1) { }
	explicit EidosValue_Float(const std::vector<double> &p_doublevec);
	explicit EidosValue_Float(std::initializer_list<double> p_init_list);
	explicit EidosValue_Float(const double *p_values, size_t p_count);
	inline virtual ~EidosValue_Float(void) override { if (values_ != &singleton_value_) free(values_); }
	
	virtual const double *FloatData(void) const override { return values_; }
	virtual double *FloatData_Mutable(void) override { WILL_MODIFY(this); return values_; }
	
	virtual const std::string &ElementType(void) const override;
	virtual EidosValue_SP NewMatchingType(void) const override;
	virtual void PrintValueAtIndex(const int p_idx, std::ostream &p_ostream) const override;
	virtual nlohmann::json JSONRepresentation(void) const override;
	
	virtual double FloatAtIndex_NOCAST(int p_idx, const EidosToken *p_blame_token) const override;
	virtual double NumericAtIndex_NOCAST(int p_idx, const EidosToken *p_blame_token) const override;	// casts integer to float, otherwise does not cast
	
	virtual eidos_logical_t LogicalAtIndex_CAST(int p_idx, const EidosToken *p_blame_token) const override;
	virtual std::string StringAtIndex_CAST(int p_idx, const EidosToken *p_blame_token) const override;
	virtual int64_t IntAtIndex_CAST(int p_idx, const EidosToken *p_blame_token) const override;
	virtual double FloatAtIndex_CAST(int p_idx, const EidosToken *p_blame_token) const override;
	
	virtual EidosValue_SP GetValueAtIndex(const int p_idx, const EidosToken *p_blame_token) const override;
	virtual EidosValue_SP CopyValues(void) const override;
	virtual EidosValue_SP VectorBasedCopy(void) const override;
	virtual void PushValueFromIndexOfEidosValue(int p_idx, const EidosValue &p_source_script_value, const EidosToken *p_blame_token) override;
	virtual void Sort(bool p_ascending) override;

	// vector lookalike methods for speed; not virtual, only for clients with an EidosValue_Float*
	EidosValue_Float *reserve(size_t p_reserved_size);				// as in std::vector
	void erase_index(size_t p_index);								// a weak substitute for erase()
	
	inline void expand(void)
	{
		// expand to fit (at least) one new value
		WILL_MODIFY(this);
		
		if (capacity_ <= 8)
			reserve(16);
		else
			reserve(capacity_ << 1);
	}
	
	inline EidosValue_Float *resize_no_initialize(size_t p_new_size)
	{
		// resizes but does not zero-initialize, unlike std::vector!
		WILL_MODIFY(this);
		
		reserve(p_new_size);	// might set a capacity greater than p_new_size; no guarantees
		count_ = p_new_size;	// regardless of the capacity set, set the size to exactly p_new_size
		
		return this;
	}
	
	inline __attribute__((always_inline)) double *data(void) { WILL_MODIFY(this); return values_; }
	inline __attribute__((always_inline)) const double *data(void) const { return values_; }
	inline __attribute__((always_inline)) void push_float(double p_float)
	{
		WILL_MODIFY(this);
		if (count_ == capacity_) expand();
		values_[count_++] = p_float;
	}
	inline __attribute__((always_inline)) void push_float_no_check(double p_float) {
		WILL_MODIFY(this);
#if DEBUG
		// do checks only in DEBUG mode, for speed; the user should never be able to trigger these errors
		if (count_ == capacity_) RaiseForCapacityViolation();
#endif
		values_[count_++] = p_float;
	}
	inline __attribute__((always_inline)) void set_float_no_check(double p_float, size_t p_index) {
		WILL_MODIFY(this);
#if DEBUG
		// do checks only in DEBUG mode, for speed; the user should never be able to trigger these errors
		if (p_index >= count_) RaiseForRangeViolation();
#endif
		values_[p_index] = p_float;
	}
};


#pragma mark -
#pragma mark EidosValue_Object
#pragma mark -

//	*********************************************************************************************************
//
//	EidosValue_Object represents objects in Eidos: entities that have properties and can respond to
//	methods.  The subclass EidosValue_Object_vector is the standard instance class, used to hold vectors
//	of objects.  EidosValue_Object_singleton is used for speed, to represent single values.
//

// EidosObject supports a retain/release mechanism that disposes of objects when no longer
// referenced, which client code can get by subclassing EidosDictionaryRetained instead of
// EidosObject and subclassing EidosDictionaryRetained_Class instead of EidosClass.
// Note that most SLiM objects have managed lifetimes, and are not under retain/release,
// but Mutation uses retain/release so that those objects can be kept permanently by the user's
// script.  Note that if you inherit from EidosDictionaryRetained you *must* subclass from
// EidosDictionaryRetained_Class, and vice versa; each is considered a guarantee of the other.

class EidosValue_Object : public EidosValue
{
private:
	typedef EidosValue super;

protected:
	const EidosClass *class_;		// can be gEidosObject_Class if the vector is empty
	bool class_uses_retain_release_;	// cached from UsesRetainRelease() of class_; true until class_ is set, to catch errors
	
	EidosValue_Object(bool p_singleton, const EidosClass *p_class);
	
	virtual int Count_Virtual(void) const override = 0;
	
public:
	EidosValue_Object(const EidosValue_Object &p_original) = delete;				// no copy-construct
	EidosValue_Object(void) = delete;												// no default constructor
	EidosValue_Object& operator=(const EidosValue_Object&) = delete;				// no copying
	virtual ~EidosValue_Object(void) override;
	
	// check the type of a new element being added to an EidosValue_Object, and update class_uses_retain_release_
	inline __attribute__((always_inline)) void DeclareClassFromElement(const EidosObject *p_element, bool p_undeclared_is_error = false)
	{
		WILL_MODIFY(this);
		
		// no check that p_element is non-null; that should always be the case, and we don't
		// want to waste the time - a crash is fine if this invariant is not satisfied
		const EidosClass *element_class = p_element->Class();
		
		if (element_class != class_)
		{
			if (!p_undeclared_is_error && (class_ == gEidosObject_Class))
			{
				class_ = element_class;
				class_uses_retain_release_ = class_->UsesRetainRelease();
			}
			else
				RaiseForClassMismatch();
		}
	}
	void RaiseForClassMismatch(void) const;

	virtual const std::string &ElementType(void) const override;
	inline __attribute__((always_inline)) const EidosClass *Class(void) const { return class_; }
	inline __attribute__((always_inline)) bool UsesRetainRelease(void) const { return class_uses_retain_release_; }
	virtual void PrintValueAtIndex(const int p_idx, std::ostream &p_ostream) const override;
	virtual nlohmann::json JSONRepresentation(void) const override;
	
	virtual EidosObject *ObjectElementAtIndex_CAST(int p_idx, const EidosToken *p_blame_token) const override = 0;
	virtual EidosObject *ObjectElementAtIndex_NOCAST(int p_idx, const EidosToken *p_blame_token) const override = 0;
	virtual EidosValue_SP GetValueAtIndex(const int p_idx, const EidosToken *p_blame_token) const override = 0;
	
	virtual EidosValue_SP CopyValues(void) const override = 0;
	virtual EidosValue_SP NewMatchingType(void) const override;
	virtual void PushValueFromIndexOfEidosValue(int p_idx, const EidosValue &p_source_script_value, const EidosToken *p_blame_token) override = 0;
	virtual void Sort(bool p_ascending) override;
	
	// Property and method support; defined only on EidosValue_Object, not EidosValue.  The methods that a
	// EidosValue_Object instance defines depend upon the type of the EidosObject objects it contains.
	virtual EidosValue_SP GetPropertyOfElements(EidosGlobalStringID p_property_id) const = 0;
	virtual void SetPropertyOfElements(EidosGlobalStringID p_property_id, const EidosValue &p_value, EidosToken *p_property_token) = 0;
	
	virtual EidosValue_SP ExecuteMethodCall(EidosGlobalStringID p_method_id, const EidosInstanceMethodSignature *p_call_signature, const std::vector<EidosValue_SP> &p_arguments, EidosInterpreter &p_interpreter) = 0;
	
	// Provided to SLiM for the Mutation-pointer hack; see EidosValue_Object::EidosValue_Object() for comments
	virtual void PatchPointersByAdding(std::uintptr_t p_pointer_difference) = 0;
	virtual void PatchPointersBySubtracting(std::uintptr_t p_pointer_difference) = 0;
};

class EidosValue_Object_vector final : public EidosValue_Object
{
private:
	typedef EidosValue_Object super;

protected:
	EidosObject **values_ = nullptr;		// these may use a retain/release system of ownership; see below
	size_t count_ = 0, capacity_ = 0;
	
	virtual int Count_Virtual(void) const override { return (int)count_; }
	
public:
	EidosValue_Object_vector(const EidosValue_Object_vector &p_original);				// can copy-construct
	EidosValue_Object_vector& operator=(const EidosValue_Object_vector&) = delete;		// no copying
	
	explicit inline EidosValue_Object_vector(const EidosClass *p_class) : EidosValue_Object(false, p_class) { }		// can be gEidosObject_Class
	explicit EidosValue_Object_vector(const std::vector<EidosObject *> &p_elementvec, const EidosClass *p_class);
	//explicit EidosValue_Object_vector(EidosObject *p_element1);		// disabled to encourage use of EidosValue_Object_singleton for this case
	explicit EidosValue_Object_vector(std::initializer_list<EidosObject *> p_init_list, const EidosClass *p_class);
	explicit EidosValue_Object_vector(EidosObject **p_values, size_t p_count, const EidosClass *p_class);
	virtual ~EidosValue_Object_vector(void) override;
	
	virtual EidosObject *ObjectElementAtIndex_NOCAST(int p_idx, const EidosToken *p_blame_token) const override;
	
	virtual EidosObject *ObjectElementAtIndex_CAST(int p_idx, const EidosToken *p_blame_token) const override;
	
	virtual EidosObject * const *ObjectData(void) const override { return values_; }
	virtual EidosObject **ObjectData_Mutable(void) override { WILL_MODIFY(this); return values_; }
	
	virtual EidosValue_SP GetValueAtIndex(const int p_idx, const EidosToken *p_blame_token) const override;
	
	virtual EidosValue_SP CopyValues(void) const override;
	virtual void PushValueFromIndexOfEidosValue(int p_idx, const EidosValue &p_source_script_value, const EidosToken *p_blame_token) override;
	void SortBy(const std::string &p_property, bool p_ascending);
	
	// Property and method support; defined only on EidosValue_Object, not EidosValue.  The methods that a
	// EidosValue_Object instance defines depend upon the type of the EidosObject objects it contains.
	virtual EidosValue_SP GetPropertyOfElements(EidosGlobalStringID p_property_id) const override;
	virtual void SetPropertyOfElements(EidosGlobalStringID p_property_id, const EidosValue &p_value, EidosToken *p_property_token) override;
	
	virtual EidosValue_SP ExecuteMethodCall(EidosGlobalStringID p_method_id, const EidosInstanceMethodSignature *p_call_signature, const std::vector<EidosValue_SP> &p_arguments, EidosInterpreter &p_interpreter) override;
	
	// Provided to SLiM for the Mutation-pointer hack; see EidosValue_Object::EidosValue_Object() for comments
	virtual void PatchPointersByAdding(std::uintptr_t p_pointer_difference) override;
	virtual void PatchPointersBySubtracting(std::uintptr_t p_pointer_difference) override;
	
	// vector lookalike methods; not virtual, only for clients with a EidosValue_Int_vector*
	void clear(void);													// as in std::vector
	EidosValue_Object_vector *reserve(size_t p_reserved_size);			// as in std::vector
	EidosValue_Object_vector *resize_no_initialize(size_t p_new_size);	// does not zero-initialize, unlike std::vector!
	EidosValue_Object_vector *resize_no_initialize_RR(size_t p_new_size);	// doesn't zero-initialize even for the RR case (set_object_element_no_check_RR may not be used, use set_object_element_no_check_no_previous_RR)
	void expand(void);													// expand to fit (at least) one new value
	void erase_index(size_t p_index);									// a weak substitute for erase()
	
	inline __attribute__((always_inline)) EidosObject **data(void) { WILL_MODIFY(this); return values_; }		// the accessors below should be used to modify, since they handle Retain()/Release()
	inline __attribute__((always_inline)) EidosObject * const *data(void) const { return values_; }
	
	// fast accessors; you can use the _RR or _NORR versions in a tight loop to avoid overhead, when you know
	// whether the EidosObject subclass you are using inherits from EidosDictionaryRetained or not;
	// you can call UsesRetainRelease() to find that out if you don't know or want to assert() for safety
	void push_object_element_CRR(EidosObject *p_object);								// checks for retain/release
	void push_object_element_RR(EidosObject *p_object);								// specifies retain/release
	void push_object_element_NORR(EidosObject *p_object);							// specifies no retain/release
	
	void push_object_element_capcheck_NORR(EidosObject *p_object);					// specifies no retain/release; capacity check only
	
	void push_object_element_no_check_CRR(EidosObject *p_object);					// checks for retain/release
	void push_object_element_no_check_RR(EidosObject *p_object);						// specifies retain/release
	void push_object_element_no_check_NORR(EidosObject *p_object);					// specifies no retain/release
	void push_object_element_no_check_already_retained(EidosObject *p_object);		// specifies retain/release is IGNORED
	
	void set_object_element_no_check_CRR(EidosObject *p_object, size_t p_index);		// checks for retain/release
	void set_object_element_no_check_RR(EidosObject *p_object, size_t p_index);		// specifies retain/release
	void set_object_element_no_check_no_previous_RR(EidosObject *p_object, size_t p_index);		// specifies retain/release, previous value assumed invalid from resize_no_initialize_RR
	void set_object_element_no_check_NORR(EidosObject *p_object, size_t p_index);	// specifies no retain/release
};

inline __attribute__((always_inline)) void EidosValue_Object_vector::push_object_element_CRR(EidosObject *p_object)
{
	WILL_MODIFY(this);
	
	if (count_ == capacity_)
		expand();
	
	DeclareClassFromElement(p_object);
	
	if (class_uses_retain_release_)
		static_cast<EidosDictionaryRetained *>(p_object)->Retain();		// unsafe cast to avoid virtual function overhead
	
	values_[count_++] = p_object;
}

inline __attribute__((always_inline)) void EidosValue_Object_vector::push_object_element_RR(EidosObject *p_object)
{
	WILL_MODIFY(this);
	
#if DEBUG
	// do checks only in DEBUG mode, for speed; the user should never be able to trigger these errors
	if (!class_uses_retain_release_) RaiseForRetainReleaseViolation();
#endif
	
	if (count_ == capacity_)
		expand();
	
	DeclareClassFromElement(p_object);
	
	static_cast<EidosDictionaryRetained *>(p_object)->Retain();		// unsafe cast to avoid virtual function overhead
	
	values_[count_++] = p_object;
}

inline __attribute__((always_inline)) void EidosValue_Object_vector::push_object_element_NORR(EidosObject *p_object)
{
	WILL_MODIFY(this);
	
#if DEBUG
	// do checks only in DEBUG mode, for speed; the user should never be able to trigger these errors
	if (class_uses_retain_release_) RaiseForRetainReleaseViolation();
#endif
	
	if (count_ == capacity_)
		expand();
	
	DeclareClassFromElement(p_object);
	
	values_[count_++] = p_object;
}

inline __attribute__((always_inline)) void EidosValue_Object_vector::push_object_element_capcheck_NORR(EidosObject *p_object)
{
	WILL_MODIFY(this);
	
#if DEBUG
	// do checks only in DEBUG mode, for speed; the user should never be able to trigger these errors
	DeclareClassFromElement(p_object, true);				// require a prior matching declaration
	if (class_uses_retain_release_) RaiseForRetainReleaseViolation();
#endif
	
	if (count_ == capacity_)
		expand();
	
	values_[count_++] = p_object;
}

inline __attribute__((always_inline)) void EidosValue_Object_vector::push_object_element_no_check_CRR(EidosObject *p_object)
{
	WILL_MODIFY(this);
	
#if DEBUG
	// do checks only in DEBUG mode, for speed; the user should never be able to trigger these errors
	if (count_ == capacity_) RaiseForCapacityViolation();
	DeclareClassFromElement(p_object, true);				// require a prior matching declaration
#endif
	
	if (class_uses_retain_release_)
		static_cast<EidosDictionaryRetained *>(p_object)->Retain();		// unsafe cast to avoid virtual function overhead
	
	values_[count_++] = p_object;
}

inline __attribute__((always_inline)) void EidosValue_Object_vector::push_object_element_no_check_RR(EidosObject *p_object)
{
	WILL_MODIFY(this);
	
#if DEBUG
	// do checks only in DEBUG mode, for speed; the user should never be able to trigger these errors
	if (count_ == capacity_) RaiseForCapacityViolation();
	DeclareClassFromElement(p_object, true);				// require a prior matching declaration
	if (!class_uses_retain_release_) RaiseForRetainReleaseViolation();
#endif
	
	static_cast<EidosDictionaryRetained *>(p_object)->Retain();		// unsafe cast to avoid virtual function overhead
	
	values_[count_++] = p_object;
}

inline __attribute__((always_inline)) void EidosValue_Object_vector::push_object_element_no_check_NORR(EidosObject *p_object)
{
	WILL_MODIFY(this);
	
#if DEBUG
	// do checks only in DEBUG mode, for speed; the user should never be able to trigger these errors
	if (count_ == capacity_) RaiseForCapacityViolation();
	DeclareClassFromElement(p_object, true);				// require a prior matching declaration
	if (class_uses_retain_release_) RaiseForRetainReleaseViolation();
#endif
	
	values_[count_++] = p_object;
}

inline __attribute__((always_inline)) void EidosValue_Object_vector::push_object_element_no_check_already_retained(EidosObject *p_object)
{
	WILL_MODIFY(this);
	
#if DEBUG
	// do checks only in DEBUG mode, for speed; the user should never be able to trigger these errors
	if (count_ == capacity_) RaiseForCapacityViolation();
	DeclareClassFromElement(p_object, true);				// require a prior matching declaration
	// no retain/release check even in DEBUG; the caller says they know what they are doing
#endif
	
	values_[count_++] = p_object;
}

inline __attribute__((always_inline)) void EidosValue_Object_vector::set_object_element_no_check_CRR(EidosObject *p_object, size_t p_index)
{
	WILL_MODIFY(this);
	
#if DEBUG
	// do checks only in DEBUG mode, for speed; the user should never be able to trigger these errors
	if (p_index >= count_) RaiseForRangeViolation();
	DeclareClassFromElement(p_object, true);				// require a prior matching declaration
#endif
	EidosObject *&value_slot_to_replace = values_[p_index];
	
	if (class_uses_retain_release_)
	{
		static_cast<EidosDictionaryRetained *>(p_object)->Retain();						// unsafe cast to avoid virtual function overhead
		if (value_slot_to_replace)
			static_cast<EidosDictionaryRetained *>(value_slot_to_replace)->Release();	// unsafe cast to avoid virtual function overhead
	}
	
	value_slot_to_replace = p_object;
}

inline __attribute__((always_inline)) void EidosValue_Object_vector::set_object_element_no_check_RR(EidosObject *p_object, size_t p_index)
{
	WILL_MODIFY(this);
	
#if DEBUG
	// do checks only in DEBUG mode, for speed; the user should never be able to trigger these errors
	if (p_index >= count_) RaiseForRangeViolation();
	DeclareClassFromElement(p_object, true);				// require a prior matching declaration
	if (!class_uses_retain_release_) RaiseForRetainReleaseViolation();
#endif
	EidosObject *&value_slot_to_replace = values_[p_index];
	
	static_cast<EidosDictionaryRetained *>(p_object)->Retain();						// unsafe cast to avoid virtual function overhead
	if (value_slot_to_replace)
		static_cast<EidosDictionaryRetained *>(value_slot_to_replace)->Release();	// unsafe cast to avoid virtual function overhead
	
	value_slot_to_replace = p_object;
}

inline __attribute__((always_inline)) void EidosValue_Object_vector::set_object_element_no_check_no_previous_RR(EidosObject *p_object, size_t p_index)
{
	WILL_MODIFY(this);
	
#if DEBUG
	// do checks only in DEBUG mode, for speed; the user should never be able to trigger these errors
	if (p_index >= count_) RaiseForRangeViolation();
	DeclareClassFromElement(p_object, true);				// require a prior matching declaration
	if (!class_uses_retain_release_) RaiseForRetainReleaseViolation();
#endif
	EidosObject *&value_slot_to_replace = values_[p_index];
	
	static_cast<EidosDictionaryRetained *>(p_object)->Retain();						// unsafe cast to avoid virtual function overhead
	
	value_slot_to_replace = p_object;
}

inline __attribute__((always_inline)) void EidosValue_Object_vector::set_object_element_no_check_NORR(EidosObject *p_object, size_t p_index)
{
	WILL_MODIFY(this);
	
#if DEBUG
	// do checks only in DEBUG mode, for speed; the user should never be able to trigger these errors
	if (p_index >= count_) RaiseForRangeViolation();
	DeclareClassFromElement(p_object, true);				// require a prior matching declaration
	if (class_uses_retain_release_) RaiseForRetainReleaseViolation();
#endif
	EidosObject *&value_slot_to_replace = values_[p_index];
	
	value_slot_to_replace = p_object;
}

class EidosValue_Object_singleton final : public EidosValue_Object
{
private:
	typedef EidosValue_Object super;

protected:
	EidosObject *value_;		// these may use a retain/release system of ownership; see below
	
	virtual int Count_Virtual(void) const override { return 1; }
	
public:
	EidosValue_Object_singleton(const EidosValue_Object_singleton &p_original) = delete;		// no copy-construct
	EidosValue_Object_singleton& operator=(const EidosValue_Object_singleton&) = delete;		// no copying
	EidosValue_Object_singleton(void) = delete;
	explicit EidosValue_Object_singleton(EidosObject *p_element1, const EidosClass *p_class);
	explicit EidosValue_Object_singleton(EidosObject *p_element1, const EidosClass *p_class, bool p_register_for_patching);	// a variant for self-pointer EidosValues; not for general use
	virtual ~EidosValue_Object_singleton(void) override;
	
	virtual EidosObject *ObjectElementAtIndex_NOCAST(int p_idx, const EidosToken *p_blame_token) const override;
	
	virtual EidosObject *ObjectElementAtIndex_CAST(int p_idx, const EidosToken *p_blame_token) const override;
	
	virtual EidosObject * const *ObjectData(void) const override { return &value_; }
	virtual EidosObject **ObjectData_Mutable(void) override { WILL_MODIFY(this); return &value_; }		// very dangerous; do not use
	void SetValue(EidosObject *p_element);																		// very dangerous; used only in Evaluate_For()
	
	virtual EidosValue_SP GetValueAtIndex(const int p_idx, const EidosToken *p_blame_token) const override;
	virtual EidosValue_SP CopyValues(void) const override;
	
	virtual EidosValue_SP VectorBasedCopy(void) const override;
	
	// prohibited actions because there is no backing vector
	virtual void PushValueFromIndexOfEidosValue(int p_idx, const EidosValue &p_source_script_value, const EidosToken *p_blame_token) override;
	
	// Property and method support; defined only on EidosValue_Object, not EidosValue.  The methods that a
	// EidosValue_Object instance defines depend upon the type of the EidosObject objects it contains.
	virtual EidosValue_SP GetPropertyOfElements(EidosGlobalStringID p_property_id) const override;
	virtual void SetPropertyOfElements(EidosGlobalStringID p_property_id, const EidosValue &p_value, EidosToken *p_property_token) override;
	
	virtual EidosValue_SP ExecuteMethodCall(EidosGlobalStringID p_method_id, const EidosInstanceMethodSignature *p_call_signature, const std::vector<EidosValue_SP> &p_arguments, EidosInterpreter &p_interpreter) override;
	
	// Provided to SLiM for the Mutation-pointer hack; see EidosValue_Object::EidosValue_Object() for comments
	virtual void PatchPointersByAdding(std::uintptr_t p_pointer_difference) override;
	virtual void PatchPointersBySubtracting(std::uintptr_t p_pointer_difference) override;
	
	// We support just this method, from EidosValue_Object_vector's suite of setters, for now
	void set_object_element_no_check_CRR(EidosObject *p_object, size_t p_index);		// checks for retain/release
};

inline __attribute__((always_inline)) void EidosValue_Object_singleton::set_object_element_no_check_CRR(EidosObject *p_object, __attribute__((unused)) size_t p_index)
{
	WILL_MODIFY(this);
	
#if DEBUG
	// do checks only in DEBUG mode, for speed; the user should never be able to trigger these errors
	if (p_index >= 1) RaiseForRangeViolation();
	DeclareClassFromElement(p_object, true);				// require a prior matching declaration
#endif
	if (class_uses_retain_release_)
	{
		static_cast<EidosDictionaryRetained *>(p_object)->Retain();						// unsafe cast to avoid virtual function overhead
		if (value_)
			static_cast<EidosDictionaryRetained *>(value_)->Release();	// unsafe cast to avoid virtual function overhead
	}
	
	value_ = p_object;
}


#endif /* defined(__Eidos__eidos_value__) */




































































