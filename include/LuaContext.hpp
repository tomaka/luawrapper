/*
Copyright (c) 2013, Pierre KRIEGER
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the <organization> nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef INCLUDE_LUACONTEXT_HPP
#define INCLUDE_LUACONTEXT_HPP

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <sstream>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <boost/any.hpp>
#include <boost/mpl/distance.hpp>
#include <boost/mpl/transform.hpp>
#include <boost/optional.hpp>
#include <boost/variant.hpp>
#include <boost/type_traits.hpp>
#include <lua.hpp>

#ifdef _MSC_VER
#	include "misc/exception.hpp"
#endif

/**
 * Defines a Lua context
 * A Lua context is used to interpret Lua code. Since everything in Lua is a variable (including functions),
 * we only provide few functions like readVariable and writeVariable.
 *
 * You can also write variables with C++ functions so that they are callable by Lua. Note however that you HAVE TO convert
 * your function to std::function (not directly std::bind or a lambda function) so the class can detect which argument types
 * it wants. These arguments may only be of basic types (int, float, etc.) or std::string.
 */
class LuaContext {
	struct ValueInRegistry;
public:
	/**
	 * @param openDefaultLibs True if luaL_openlibs should be called
	 */
	explicit LuaContext(bool openDefaultLibs = true)
	{
		// luaL_newstate can return null if allocation failed
		mState = luaL_newstate();
		if (mState == nullptr)		throw std::bad_alloc();

		// setting the panic function
		lua_atpanic(mState, [](lua_State* state) -> int {
			const std::string str = lua_tostring(state, -1);
			lua_pop(state, 1);
			assert(false && "lua_atpanic triggered");
			exit(0);
		});

		// opening default library if required to do so
		if (openDefaultLibs)
			luaL_openlibs(mState);
	}

	/**
	 * Move constructor
	 */
	LuaContext(LuaContext&& s) :
		mState(s.mState)
	{
		s.mState = luaL_newstate();
	}
	
	/**
	 * Move operator
	 */
	LuaContext& operator=(LuaContext&& s)
	{
		std::swap(mState, s.mState);
		return *this;
	}

	/**
	 * Copy is forbidden
	 */
	LuaContext(const LuaContext&) = delete;
	
	/**
	 * Copy is forbidden
	 */
	LuaContext& operator=(const LuaContext&) = delete;

	/**
	 * Destructor
	 */
	~LuaContext()
	{
		assert(mState);
		lua_close(mState);
	}
	
	/**
	 * Thrown when an error happens during execution of lua code (like not enough parameters for a function)
	 */
	class ExecutionErrorException : public std::runtime_error
	{
	public:
		ExecutionErrorException(const std::string& msg) :
			std::runtime_error(msg)
		{
		}
	};

	/**
	 * Thrown when a syntax error happens in a lua script
	 */
	class SyntaxErrorException : public std::runtime_error
	{
	public:
		SyntaxErrorException(const std::string& msg) :
			std::runtime_error(msg)
		{
		}
	};

	/**
	 * Thrown when trying to cast a Lua variable to an unvalid type, eg. trying to read a number when the variable is a string
	 */
	class WrongTypeException : public std::runtime_error
	{
	public:
		WrongTypeException(std::string luaType, const std::type_info& destination) :
			std::runtime_error("Trying to cast a lua variable from \"" + luaType + "\" to \"" + destination.name() + "\""),
			luaType(luaType),
			destination(destination)
		{
		}
		
		std::string luaType;
		const std::type_info& destination;
	};

	/**
	 * Function object that can call a function stored by Lua
	 * This type is copiable and movable, but not constructible. It can only be created through readVariable.
	 * @tparam TFunctionType	Function type (eg. "int (int, bool)")
	 */
	template<typename TFunctionType>
	class LuaFunctionCaller;

	/**
	 * Opaque type that identifies a Lua thread
	 */
	struct ThreadID {
		ThreadID() = default;
		ThreadID(ThreadID&& o) : state(o.state), threadInRegistry(std::move(o.threadInRegistry)) { }
		ThreadID& operator=(ThreadID&& o) { std::swap(state, o.state); std::swap(threadInRegistry, o.threadInRegistry); return *this; }
	public:
		friend LuaContext;
		lua_State* state;
		std::unique_ptr<ValueInRegistry> threadInRegistry;
	};

	/**
	 * Type that is considered as an empty array
	 */
	enum EmptyArray_t { EmptyArray };

	/**
	 * Type for a metatable
	 */
	enum Metatable_t { Metatable };

	/**
	 * Executes lua code from the stream
	 * @param code		A stream that Lua will read its code from
	 */
	void executeCode(std::istream& code)
	{
		load(mState, code);
		call<std::tuple<>>(mState);
	}

	/**
	 * Executes lua code from the stream and returns a value
	 * @param code		A stream that Lua will read its code from
	 * @tparam TType	The type that the executing code should return
	 */
	template<typename TType>
	auto executeCode(std::istream& code)
		-> TType
	{
		load(mState, code);
		return call<TType>(mState);
	}

	/**
	 * Executes lua code given as parameter
	 * @param code		A string containing code that will be executed by Lua
	 */
	void executeCode(const std::string& code)
	{
		executeCode(code.c_str());
	}
	
	/*
	 * Executes Lua code from the stream and returns a value
	 * @param code		A string containing code that will be executed by Lua
	 * @tparam TType	The type that the executing code should return
	 */
	template<typename TType>
	auto executeCode(const std::string& code)
		-> TType
	{
		return executeCode<TType>(code.c_str());
	}

	/**
	 * Executes Lua code
	 * @param code		A string containing code that will be executed by Lua
	 */
	void executeCode(const char* code)
	{
		load(mState, code);
		call<std::tuple<>>(mState);
	}

	/*
	 * Executes Lua code from the stream and returns a value
	 * @param code		A string containing code that will be executed by Lua
	 * @tparam TType	The type that the executing code should return
	 */
	template<typename TType>
	auto executeCode(const char* code)
		-> TType
	{
		load(mState, code);
		return call<TType>(mState);
	}
	
	/**
	 * Tells that Lua will be allowed to access an object's function
	 * This is the version "registerFunction(name, &Foo::function)"
	 */
	template<typename TPointerToMemberFunction>
	auto registerFunction(const std::string& name, TPointerToMemberFunction pointer)
		-> typename std::enable_if<std::is_member_function_pointer<TPointerToMemberFunction>::value>::type
	{
		registerFunctionImpl(name, std::mem_fn(pointer), tag<TPointerToMemberFunction>{});
	}

	/**
	 * Tells that Lua will be allowed to access an object's function
	 * This is the version with an explicit template parameter: "registerFunction<void (Foo::*)()>(name, [](Foo&) { })"
	 * @param fn				Function object which takes as first parameter a reference to the object
	 * @tparam TFunctionType	Pointer-to-member function type
	 */
	template<typename TFunctionType, typename TType>
	void registerFunction(const std::string& functionName, TType fn)
	{
		static_assert(std::is_member_function_pointer<TFunctionType>::value, "registerFunction must take a member function pointer type as template parameter");
		registerFunctionImpl(functionName, std::move(fn), tag<TFunctionType>{});
	}

	/**
	 * Tells that Lua will be allowed to access an object's function
	 * This is the alternative version with an explicit template parameter: "registerFunction<Foo, void (*)()>(name, [](Foo&) { })"
	 * @param fn				Function object which takes as first parameter a reference to the object
	 * @tparam TObject			Object to register this function to
	 * @tparam TFunctionType	Function type
	 */
	template<typename TObject, typename TFunctionType, typename TType>
	void registerFunction(const std::string& functionName, TType fn)
	{
		static_assert(std::is_function<TFunctionType>::value, "registerFunction must take a function type as template parameter");
		registerFunctionImpl(functionName, std::move(fn), tag<TObject>{}, tag<TFunctionType>{});
	}

	/**
	 * Inverse operation of registerFunction
	 * @tparam TType Type whose function belongs to
	 */
	template<typename TType>
	void unregisterFunction(const std::string& functionName)
	{
		lua_pushlightuserdata(mState, const_cast<std::type_info*>(&typeid(TType)));
		lua_pushnil(mState);
		lua_settable(mState, LUA_REGISTRYINDEX);
		checkTypeRegistration(mState, &typeid(TType));
		
		lua_pushlightuserdata(mState, const_cast<std::type_info*>(&typeid(TType*)));
		lua_pushnil(mState);
		lua_settable(mState, LUA_REGISTRYINDEX);
		checkTypeRegistration(mState, &typeid(TType*));
		
		lua_pushlightuserdata(mState, const_cast<std::type_info*>(&typeid(std::shared_ptr<TType>)));
		lua_pushnil(mState);
		lua_settable(mState, LUA_REGISTRYINDEX);
		checkTypeRegistration(mState, &typeid(std::shared_ptr<TType>));
	}
	
	/**
	 * Registers a member variable
	 * This is the version "registerMember(name, &Foo::member)"
	 */
	template<typename TObject, typename TVarType>
	void registerMember(const std::string& name, TVarType TObject::*member)
	{
		// implementation simply calls the custom member with getter and setter
		const auto getter = [=](const TObject& obj) -> TVarType { return obj.*member; };
		const auto setter = [=](TObject& obj, const TVarType& value) { obj.*member = value; };
		registerMember<TVarType (TObject::*)>(name, getter, setter);
	}

	/**
	 * Registers a member variable
	 * This is the version "registerMember<Foo, int>(name, getter, setter)"
	 * @tparam TObject 		 Type to register the member to
	 * @tparam TVarType		 Type of the member
	 * @param name			 Name of the member to register
	 * @param readFunction	 Function of type "TVarType (const TObject&)"
	 * @param writeFunction	 Function of type "void (TObject&, const TVarType&)"
	 */
	template<typename TObject, typename TVarType, typename TReadFunction, typename TWriteFunction>
	void registerMember(const std::string& name, TReadFunction readFunction, TWriteFunction writeFunction)
	{
		registerMemberImpl<TObject,TVarType>(name, std::move(readFunction), std::move(writeFunction));
	}

	/**
	 * Registers a member variable
	 * This is the version "registerMember<int (Foo::*)>(name, getter, setter)"
	 * @tparam TMemberType 	 Pointer to member object representing the type
	 * @param name			 Name of the member to register
	 * @param readFunction	 Function of type "TVarType (const TObject&)"
	 * @param writeFunction	 Function of type "void (TObject&, const TVarType&)"
	 */
	template<typename TMemberType, typename TReadFunction, typename TWriteFunction>
	void registerMember(const std::string& name, TReadFunction readFunction, TWriteFunction writeFunction)
	{
		static_assert(std::is_member_object_pointer<TMemberType>::value, "registerMember must take a member object pointer type as template parameter");
		registerMemberImpl(tag<TMemberType>{}, name, std::move(readFunction), std::move(writeFunction));
	}

	/**
	 * Registers a non-modifiable member variable
	 * This is the version "registerMember<Foo, int>(name, getter)"
	 * @tparam TObject 		 Type to register the member to
	 * @tparam TVarType		 Type of the member
	 * @param name			 Name of the member to register
	 * @param readFunction	 Function of type "TVarType (const TObject&)"
	 */
	template<typename TObject, typename TVarType, typename TReadFunction>
	void registerMember(const std::string& name, TReadFunction readFunction)
	{
		registerMemberImpl<TObject,TVarType>(name, std::move(readFunction));
	}

	/**
	 * Registers a non-modifiable member variable
	 * This is the version "registerMember<int (Foo::*)>(name, getter)"
	 * @tparam TMemberType 	 Pointer to member object representing the type
	 * @param name			 Name of the member to register
	 * @param readFunction	 Function of type "TVarType (const TObject&)"
	 */
	template<typename TMemberType, typename TReadFunction>
	void registerMember(const std::string& name, TReadFunction readFunction)
	{
		static_assert(std::is_member_object_pointer<TMemberType>::value, "registerMember must take a member object pointer type as template parameter");
		registerMemberImpl(tag<TMemberType>{}, name, std::move(readFunction));
	}

	/**
	 * Registers a dynamic member variable
	 * This is the version "registerMember<Foo, int>(getter, setter)"
	 * @tparam TObject 		 Type to register the member to
	 * @tparam TVarType		 Type of the member
	 * @param readFunction	 Function of type "TVarType (const TObject&, const std::string&)"
	 * @param writeFunction	 Function of type "void (TObject&, const std::string&, const TVarType&)"
	 */
	template<typename TObject, typename TVarType, typename TReadFunction, typename TWriteFunction>
	void registerMember(TReadFunction readFunction, TWriteFunction writeFunction)
	{
		registerMemberImpl<TObject,TVarType>(std::move(readFunction), std::move(writeFunction));
	}

	/**
	 * Registers a dynamic member variable
	 * This is the version "registerMember<int (Foo::*)>(getter, setter)"
	 * @tparam TMemberType 	 Pointer to member object representing the type
	 * @param readFunction	 Function of type "TVarType (const TObject&, const std::string&)"
	 * @param writeFunction	 Function of type "void (TObject&, const std::string&, const TVarType&)"
	 */
	template<typename TMemberType, typename TReadFunction, typename TWriteFunction>
	void registerMember(TReadFunction readFunction, TWriteFunction writeFunction)
	{
		static_assert(std::is_member_object_pointer<TMemberType>::value, "registerMember must take a member object pointer type as template parameter");
		registerMemberImpl(tag<TMemberType>{}, std::move(readFunction), std::move(writeFunction));
	}

	/**
	 * Registers a dynamic non-modifiable member variable
	 * This is the version "registerMember<Foo, int>(getter)"
	 * @tparam TObject 		 Type to register the member to
	 * @tparam TVarType		 Type of the member
	 * @param readFunction	 Function of type "TVarType (const TObject&, const std::string&)"
	 */
	template<typename TObject, typename TVarType, typename TReadFunction>
	void registerMember(TReadFunction readFunction)
	{
		registerMemberImpl<TObject, TVarType>(std::move(readFunction));
	}

	/**
	 * Registers a dynamic non-modifiable member variable
	 * This is the version "registerMember<int (Foo::*)>(getter)"
	 * @tparam TMemberType 	 Pointer to member object representing the type
	 * @param readFunction	 Function of type "TVarType (const TObject&, const std::string&)"
	 */
	template<typename TMemberType, typename TReadFunction>
	void registerMember(TReadFunction readFunction)
	{
		static_assert(std::is_member_object_pointer<TMemberType>::value, "registerMember must take a member object pointer type as template parameter");
		registerMemberImpl(tag<TMemberType>{}, std::move(readFunction));
	}

	/**
	 * Creates a new thread
	 * A Lua thread is not really a thread, but rather an "execution stack".
	 * You can destroy the thread by calling destroyThread
	 * @sa destroyThread
	 */
	auto createThread()
		-> ThreadID
	{
		ThreadID result;

		result.state = lua_newthread(mState);
		result.threadInRegistry = std::unique_ptr<ValueInRegistry>(new ValueInRegistry(mState));
		lua_pop(mState, 1);

		return std::move(result);
	}

	/**
	 * Forks the global variables
	 * After this function call:
	 *  - existing global variables can still be read ; if they are modified in the LuaContext, then the changes are reflected
	 *  - writing over an existing global variable will hide the existing one but not write over it
	 */
	void forkGlobals(const ThreadID& id)
	{
#		if LUA_VERSION_NUM >= 502
			
			lua_newtable(id.state);
			lua_newtable(id.state);
			lua_pushliteral(id.state, "__index");
			lua_pushglobaltable(id.state);
			lua_settable(id.state, -3);
			lua_setmetatable(id.state, -2);
			lua_pushinteger(id.state, LUA_RIDX_GLOBALS);
			lua_settable(id.state, LUA_REGISTRYINDEX);

#		else

			lua_newtable(id.state);
			lua_newtable(id.state);
			lua_pushliteral(id.state, "__index");
			lua_pushvalue(id.state, LUA_GLOBALSINDEX);
			lua_settable(id.state, -3);
			lua_setmetatable(id.state, -2);
			lua_replace(id.state, LUA_GLOBALSINDEX);

#		endif
	}

	/**
	 * Destroys a thread created with createThread
	 * @sa createThread
	 */
	void destroyThread(ThreadID& id)
	{
		id.threadInRegistry.reset();
	}
	
	/**
	 * Returns true if the value of the variable is an array
	 * @param variableName Name of the variable to check
	 * @deprecated
	 */
	bool isVariableArray(const std::string& variableName) const
	{
		return isVariableArray(variableName.c_str());
	}

	/**
	 * @sa isVariableArray
	 * @deprecated
	 */
	bool isVariableArray(const char* variableName) const
	{
		lua_getglobal(mState, variableName);
		bool answer = lua_istable(mState, -1);
		lua_pop(mState, 1);
		return answer;
	}
	
	/**
	 * Returns true if variable exists (ie. not nil)
	 * @deprecated
	 */
	bool hasVariable(const std::string& variableName) const	
	{
		lua_getglobal(mState, variableName.c_str());
		bool answer = lua_isnil(mState, -1);
		lua_pop(mState, 1);
		return !answer;
	}

	/**
	 * Reads the content of a Lua variable
	 * 
	 * @tparam TType				Type requested for the read
	 * @throw WrongTypeException	When the variable is not convertible to the requested type
	 * @sa writeVariable
	 *
	 * Readable types are all types accepted by writeVariable except nullptr, std::unique_ptr and function pointers
	 * Additionaly supported:
	 *  - LuaFunctionCaller<FunctionType>, which is an alternative to std::function
	 *  - references to custom objects, in which case it will return the object in-place
	 *
	 * After the variable name, you can add other parameters.
	 * If the variable is an array, it will instead get the element of that array whose offset is the second parameter.
	 * Same applies for third, fourth, etc. parameters.
	*/
	template<typename TType, typename... TNestedTypes>
	TType readVariable(const std::string& variableName, TNestedTypes&&... nestedElements) const
	{
		lua_getglobal(mState, variableName.c_str());
		lookIntoStackTop(mState, std::forward<TNestedTypes>(nestedElements)...);
		return readTopAndPop<TType>(mState, 1);
	}
	
	/**
	 * @sa readVariable
	 */
	template<typename TType, typename... TNestedTypes>
	TType readVariable(const char* variableName, TNestedTypes&&... nestedElements) const
	{
		lua_getglobal(mState, variableName);
		lookIntoStackTop(mState, std::forward<TNestedTypes>(nestedElements)...);
		return readTopAndPop<TType>(mState, 1);
	}

	/**
	 * Changes the content of a Lua variable
	 * 
	 * Accepted values are:
	 * - all base types (char, short, int, float, double, bool)
	 * - std::string
	 * - enums
	 * - std::vector<>
	 * - std::vector<std::pair<>>, std::map<> and std::unordered_map<> (the key and value must also be accepted values)
	 * - std::function<> (all parameters must be accepted values, and return type must be either an accepted value for readVariable or a tuple)
	 * - std::shared_ptr<> (std::unique_ptr<> are converted to std::shared_ptr<>)
	 * - nullptr (writes nil)
	 * - any object
	 *
	 * All objects are passed by copy and destroyed by the garbage collector if necessary.
	 */
	template<typename... TData>
	void writeVariable(TData&&... data) {
		static_assert(sizeof...(TData) >= 2, "You must pass at least a variable name and a value to writeVariable");
		typedef typename std::decay<typename std::tuple_element<sizeof...(TData) - 1,std::tuple<TData...>>::type>::type
			RealDataType;
		static_assert(!std::is_same<typename Tupleizer<RealDataType>::type,RealDataType>::value, "Error: you can't use LuaContext::writeVariable with a tuple");

#		if LUA_VERSION_NUM >= 502
			lua_pushglobaltable(mState);
			try {
				setTable<-1,RealDataType>(mState, std::forward<TData>(data)...);
			} catch(...) {
				lua_pop(mState, 1);
				throw;
			}
			lua_pop(mState, 1);
#		else
			setTable<LUA_GLOBALSINDEX,RealDataType>(mState, std::forward<TData>(data)...);
#		endif
	}
	
	/**
	 * Equivalent to writeVariable(varName, ..., std::function<TFunctionType>(data));
	 * This version is more effecient than writeVariable if you want to write functions
	 */
	template<typename TFunctionType, typename... TData>
	void writeFunction(TData&&... data) {
		static_assert(sizeof...(TData) >= 2, "You must pass at least a variable name and a value to writeFunction");
		
#		if LUA_VERSION_NUM >= 502
			lua_pushglobaltable(mState);
			try {
				setTable<-1,TFunctionType>(mState, std::forward<TData>(data)...);
			} catch(...) {
				lua_pop(mState, 1);
				throw;
			}
			lua_pop(mState, 1);
#		else
			setTable<LUA_GLOBALSINDEX,TFunctionType>(mState, std::forward<TData>(data)...);
#		endif
	}

	/**
	 * Same as the other writeFunction, except that the template parameter is automatically detected
	 * This only works if the data is either a native function pointer, or contains one operator() (this is the case for lambdas)
	 */
	template<typename... TData>
	void writeFunction(TData&&... data) {
		static_assert(sizeof...(TData) >= 2, "You must pass at least a variable name and a value to writeFunction");
		typedef typename std::decay<typename std::tuple_element<sizeof...(TData) - 1,std::tuple<TData...>>::type>::type
			RealDataType;
		typedef typename FunctionTypeDetector<RealDataType>::type
			DetectedFunctionType;
		
		return writeFunction<DetectedFunctionType>(std::forward<TData>(data)...);
	}
	

private:
	// the state is the most important variable in the class since it is our interface with Lua
	//  - registered members and functions are stored in tables at offset &typeid(type) of the registry
	//    each table has its getter functions at offset 0, getter members at offset 1, default getter at offset 2
	//	  offset 3 is unused, setter members at offset 4, default setter at offset 5
	lua_State*					mState;
	

	/**************************************************/
	/*                     MISC                       */
	/**************************************************/
	// type used as a tag
	template<typename T>
	struct tag {};

	// this function takes a value representing the offset to look into
	// it will look into the top element of the stack and replace the element by its content at the given index
	template<typename OffsetType1, typename... OffsetTypeOthers>
	static void lookIntoStackTop(lua_State* state, OffsetType1&& offset1, OffsetTypeOthers&&... offsetOthers) {
		static_assert(Pusher<typename std::decay<OffsetType1>::type>::minSize == 1 && Pusher<typename std::decay<OffsetType1>::type>::maxSize == 1, "Impossible to have a multiple-values index");
		Pusher<typename std::decay<OffsetType1>::type>::push(state, offset1);
		lua_gettable(state, -2);
		lua_remove(state, -2);

		lookIntoStackTop(state, std::forward<OffsetTypeOthers>(offsetOthers)...);
	}

	template<typename... OffsetTypeOthers>
	static void lookIntoStackTop(lua_State* state, Metatable_t, OffsetTypeOthers&&... offsetOthers) {
		lua_getmetatable(state, -1);
		lua_remove(state, -2);

		lookIntoStackTop(state, std::forward<OffsetTypeOthers>(offsetOthers)...);
	}
	
	template<typename... OffsetTypeOthers>
	static void lookIntoStackTop(lua_State*, const ThreadID& thread, OffsetTypeOthers&&... offsetOthers) {
		lookIntoStackTop(thread.state, std::forward<OffsetTypeOthers>(offsetOthers)...);
	}
	
	static void lookIntoStackTop(lua_State*) {
	}
	
	// equivalent of lua_settable with t[k]=n, where t is the value at the index in the template parameter, k is the second parameter, n is the last parameter, and n is pushed by the function in the first parameter
	// if there are more than 3 parameters, parameters 3 to n-1 are considered as sub-indices into the array
	// the dataPusher MUST push only one thing on the stack
	// TTableIndex must be either LUA_REGISTERYINDEX, LUA_GLOBALSINDEX, LUA_ENVINDEX, or the position of the element on the stack
	template<int TTableIndex, typename TDataType, typename TIndex, typename TData>
	static void setTable(lua_State* state, TIndex&& index, TData&& data)
	{
		static_assert(Pusher<typename std::decay<TIndex>::type>::minSize == 1 && Pusher<typename std::decay<TIndex>::type>::maxSize == 1, "Impossible to have a multiple-values index");
		static_assert(Pusher<typename std::decay<TDataType>::type>::minSize == 1 && Pusher<typename std::decay<TDataType>::type>::maxSize == 1, "Impossible to have a multiple-values data");
		Pusher<typename std::decay<TIndex>::type>::push(state, index);
		try { Pusher<typename std::decay<TDataType>::type>::push(state, std::forward<TData>(data)); } catch(...) { lua_pop(state, 1); throw; }
		const auto tableIndex = (TTableIndex < -100 || TTableIndex > 0) ? TTableIndex : TTableIndex - 2;
		assert(lua_istable(state, tableIndex));
		lua_settable(state, tableIndex);
	}

	template<int TTableIndex, typename TDataType, typename TData>
	static void setTable(lua_State* state, const std::string& index, TData&& data)
	{
		static_assert(Pusher<typename std::decay<TDataType>::type>::minSize == 1 && Pusher<typename std::decay<TDataType>::type>::maxSize == 1, "Impossible to have a multiple-values data");
		Pusher<typename std::decay<TDataType>::type>::push(state, std::forward<TData>(data));
		const auto tableIndex = (TTableIndex < -100 || TTableIndex > 0) ? TTableIndex : TTableIndex - 1;
		assert(lua_istable(state, tableIndex));
		lua_setfield(state, tableIndex, index.c_str());
	}

	template<int TTableIndex, typename TDataType, typename TData>
	static void setTable(lua_State* state, const char* index, TData&& data)
	{
		static_assert(Pusher<typename std::decay<TDataType>::type>::minSize == 1 && Pusher<typename std::decay<TDataType>::type>::maxSize == 1, "Impossible to have a multiple-values data");
		Pusher<typename std::decay<TDataType>::type>::push(state, std::forward<TData>(data));
		const auto tableIndex = (TTableIndex < -100 || TTableIndex > 0) ? TTableIndex : TTableIndex - 1;
		assert(lua_istable(state, tableIndex));
		lua_setfield(state, tableIndex, index);
	}

	template<int TTableIndex, typename TDataType, typename TData>
	static void setTable(lua_State* state, Metatable_t, TData&& data)
	{
		static_assert(Pusher<typename std::decay<TDataType>::type>::minSize == 1 && Pusher<typename std::decay<TDataType>::type>::maxSize == 1, "Impossible to have a multiple-values data");
		Pusher<typename std::decay<TDataType>::type>::push(state, std::forward<TData>(data));
		const auto tableIndex = (TTableIndex < -100 || TTableIndex > 0) ? TTableIndex : TTableIndex - 1;
		assert(lua_istable(state, tableIndex));
		lua_setmetatable(state, tableIndex);
	}

	template<int TTableIndex, typename TDataType, typename TIndex1, typename TIndex2, typename... TIndices>
	static auto setTable(lua_State* state, TIndex1&& index1, TIndex2&& index2, TIndices&&... indices)
		-> typename std::enable_if<!std::is_same<typename std::decay<TIndex1>::type, Metatable_t>::value>::type
	{
		static_assert(Pusher<typename std::decay<TIndex1>::type>::minSize == 1 && Pusher<typename std::decay<TIndex1>::type>::maxSize == 1, "Impossible to have a multiple-values index");
		Pusher<typename std::decay<TIndex1>::type>::push(state, std::forward<TIndex1>(index1));
		const auto tableIndex = (TTableIndex < -100 || TTableIndex > 0) ? TTableIndex : TTableIndex - 1;
		lua_gettable(state, tableIndex);
		try {
			setTable<-1,TDataType>(state, std::forward<TIndex2>(index2), std::forward<TIndices>(indices)...);
		} catch(...) {
			lua_pop(state, 1);
			throw;
		}
		lua_pop(state, 1);
	}

	template<int TTableIndex, typename TDataType, typename TIndex2, typename... TIndices>
	static void setTable(lua_State* state, Metatable_t, TIndex2&& index2, TIndices&&... indices)
	{
		if (lua_getmetatable(state, TTableIndex) == 0)
		{
			lua_newtable(state);
			const auto tableIndex = (TTableIndex < -100 || TTableIndex > 0) ? TTableIndex : TTableIndex - 1;
			try {
				setTable<-1,TDataType>(state, std::forward<TIndex2>(index2), std::forward<TIndices>(indices)...);
			} catch(...) {
				lua_pop(state, 1);
				throw;
			}
			lua_setmetatable(state, tableIndex);
		}
		else
		{
			try {
				setTable<-1,TDataType>(state, std::forward<TIndex2>(index2), std::forward<TIndices>(indices)...);
			} catch(...) {
				lua_pop(state, 1);
				throw;
			}
			lua_pop(state, 1);
		}
	}

	template<int TTableIndex, typename TDataType, typename... TIndices>
	static void setTable(lua_State* state, const ThreadID& thread, TIndices&&... indices)
	{
		setTable<-1,TDataType>(thread.state, std::forward<TIndices>(indices)...);
	}

	// simple function that reads the "nb" first top elements of the stack, pops them, and returns the value
	// warning: first parameter is the number of parameters, not the parameter index
	// if read generates an exception, stack is poped anyway
	template<typename TReturnType>
	static auto readTopAndPop(lua_State* state, int nb)
		-> TReturnType
	{
		auto val = Reader<typename std::decay<TReturnType>::type>::testRead(state, -nb);
		lua_pop(state, nb);
		if (!val.is_initialized())
			throw WrongTypeException{lua_typename(state, lua_type(state, -nb)), typeid(TReturnType)};
		return val.get();
	}

	// checks that the offsets for a type's registrations are set in the registry
	static void checkTypeRegistration(lua_State* state, const std::type_info* type)
	{
		lua_pushlightuserdata(state, const_cast<std::type_info*>(type));
		lua_gettable(state, LUA_REGISTRYINDEX);
		if (!lua_isnil(state, -1)) {
			lua_pop(state, 1);
			return;
		}
		lua_pop(state, 1);

		lua_pushlightuserdata(state, const_cast<std::type_info*>(type));
		lua_newtable(state);

		lua_pushinteger(state, 0);
		lua_newtable(state);
		lua_settable(state, -3);

		lua_pushinteger(state, 1);
		lua_newtable(state);
		lua_settable(state, -3);

		lua_pushinteger(state, 3);
		lua_newtable(state);
		lua_settable(state, -3);

		lua_pushinteger(state, 4);
		lua_newtable(state);
		lua_settable(state, -3);

		lua_settable(state, LUA_REGISTRYINDEX);
	}
	

	/**************************************************/
	/*            FUNCTIONS REGISTRATION              */
	/**************************************************/
	// the "registerFunction" public functions call this one
	template<typename TFunctionType, typename TRetValue, typename TObject, typename... TOtherParams>
	void registerFunctionImpl(const std::string& functionName, TFunctionType function, tag<TObject>, tag<TRetValue (TOtherParams...)>)
	{
		static_assert(std::is_class<TObject>::value || std::is_pointer<TObject>::value, "registerFunction can only be used for a class or a pointer");

		checkTypeRegistration(mState, &typeid(TObject));
		setTable<LUA_REGISTRYINDEX,TRetValue(TObject&, TOtherParams...)>(mState, &typeid(TObject), 0, functionName, std::move(function));
		
		checkTypeRegistration(mState, &typeid(TObject*));
		setTable<LUA_REGISTRYINDEX,TRetValue(TObject*, TOtherParams...)>(mState, &typeid(TObject*), 0, functionName, [=](TObject* obj, TOtherParams... rest) { assert(obj); return function(*obj, std::forward<TOtherParams>(rest)...); });
		
		checkTypeRegistration(mState, &typeid(std::shared_ptr<TObject>));
		setTable<LUA_REGISTRYINDEX,TRetValue(std::shared_ptr<TObject>, TOtherParams...)>(mState, &typeid(std::shared_ptr<TObject>), 0, functionName, [=](const std::shared_ptr<TObject>& obj, TOtherParams... rest) { assert(obj); return function(*obj, std::forward<TOtherParams>(rest)...); });
	}
	
	template<typename TFunctionType, typename TRetValue, typename TObject, typename... TOtherParams>
	void registerFunctionImpl(const std::string& functionName, TFunctionType function, tag<const TObject>, tag<TRetValue (TOtherParams...)> fTypeTag)
	{
		registerFunctionImpl(functionName, function, tag<TObject>{}, fTypeTag);

		checkTypeRegistration(mState, &typeid(TObject const*));
		setTable<LUA_REGISTRYINDEX,TRetValue(TObject const*, TOtherParams...)>(mState, &typeid(TObject const*), 0, functionName, [=](TObject const* obj, TOtherParams... rest) { assert(obj); return function(*obj, std::forward<TOtherParams>(rest)...); });
		
		checkTypeRegistration(mState, &typeid(std::shared_ptr<TObject const>));
		setTable<LUA_REGISTRYINDEX,TRetValue(std::shared_ptr<TObject const>, TOtherParams...)>(mState, &typeid(std::shared_ptr<TObject const>), 0, functionName, [=](const std::shared_ptr<TObject const>& obj, TOtherParams... rest) { assert(obj); return function(*obj, std::forward<TOtherParams>(rest)...); });
	}

	template<typename TFunctionType, typename TRetValue, typename TObject, typename... TOtherParams>
	void registerFunctionImpl(const std::string& functionName, TFunctionType function, tag<TRetValue (TObject::*)(TOtherParams...)>)
	{
		registerFunctionImpl(functionName, std::move(function), tag<TObject>{}, tag<TRetValue (TOtherParams...)>{});
	}

	template<typename TFunctionType, typename TRetValue, typename TObject, typename... TOtherParams>
	void registerFunctionImpl(const std::string& functionName, TFunctionType function, tag<TRetValue (TObject::*)(TOtherParams...) const>)
	{
		registerFunctionImpl(functionName, std::move(function), tag<const TObject>{}, tag<TRetValue (TOtherParams...)>{});
	}

	template<typename TFunctionType, typename TRetValue, typename TObject, typename... TOtherParams>
	void registerFunctionImpl(const std::string& functionName, TFunctionType function, tag<TRetValue (TObject::*)(TOtherParams...) volatile>)
	{
		registerFunctionImpl(functionName, std::move(function), tag<TObject>{}, tag<TRetValue (TOtherParams...)>{});
	}

	template<typename TFunctionType, typename TRetValue, typename TObject, typename... TOtherParams>
	void registerFunctionImpl(const std::string& functionName, TFunctionType function, tag<TRetValue (TObject::*)(TOtherParams...) const volatile>)
	{
		registerFunctionImpl(functionName, std::move(function), tag<const TObject>{}, tag<TRetValue (TOtherParams...)>{});
	}

	// the "registerMember" public functions call this one
	template<typename TObject, typename TVarType, typename TReadFunction>
	void registerMemberImpl(const std::string& name, TReadFunction readFunction)
	{
		static_assert(std::is_class<TObject>::value || std::is_pointer<TObject>::value, "registerMember can only be called on a class or a pointer");
		
		checkTypeRegistration(mState, &typeid(TObject));
		setTable<LUA_REGISTRYINDEX, TVarType (TObject&)>(mState, &typeid(TObject), 1, name, [readFunction](TObject const& object) {
			return readFunction(object);
		});
		
		checkTypeRegistration(mState, &typeid(TObject*));
		setTable<LUA_REGISTRYINDEX, TVarType (TObject*)>(mState, &typeid(TObject*), 1, name, [readFunction](TObject const* object) {
			assert(object);
			return readFunction(*object);
		});
		
		checkTypeRegistration(mState, &typeid(TObject const*));
		setTable<LUA_REGISTRYINDEX, TVarType (TObject const*)>(mState, &typeid(TObject const*), 1, name, [readFunction](TObject const* object) {
			assert(object);
			return readFunction(*object);
		});
		
		checkTypeRegistration(mState, &typeid(std::shared_ptr<TObject>));
		setTable<LUA_REGISTRYINDEX, TVarType (std::shared_ptr<TObject>)>(mState, &typeid(std::shared_ptr<TObject>), 1, name, [readFunction](const std::shared_ptr<TObject>& object) {
			assert(object);
			return readFunction(*object);
		});
		
		checkTypeRegistration(mState, &typeid(std::shared_ptr<TObject const>));
		setTable<LUA_REGISTRYINDEX, TVarType (std::shared_ptr<TObject const>)>(mState, &typeid(std::shared_ptr<TObject const>), 1, name, [readFunction](const std::shared_ptr<TObject const>& object) {
			assert(object);
			return readFunction(*object);
		});
	}

	template<typename TObject, typename TVarType, typename TReadFunction, typename TWriteFunction>
	void registerMemberImpl(const std::string& name, TReadFunction readFunction, TWriteFunction writeFunction)
	{
		registerMemberImpl<TObject,TVarType>(name, readFunction);

		setTable<LUA_REGISTRYINDEX, void (TObject&, TVarType)>(mState, &typeid(TObject), 4, name, [writeFunction](TObject& object, const TVarType& value) {
			writeFunction(object, value);
		});
		
		setTable<LUA_REGISTRYINDEX, void (TObject*, TVarType)>(mState, &typeid(TObject*), 4, name, [writeFunction](TObject* object, const TVarType& value) {
			assert(object);
			writeFunction(*object, value);
		});
		
		setTable<LUA_REGISTRYINDEX, void (std::shared_ptr<TObject>, TVarType)>(mState, &typeid(std::shared_ptr<TObject>), 4, name, [writeFunction](std::shared_ptr<TObject> object, const TVarType& value) {
			assert(object);
			writeFunction(*object, value);
		});
	}

	template<typename TObject, typename TVarType, typename TReadFunction, typename TWriteFunction>
	void registerMemberImpl(tag<TVarType (TObject::*)>, const std::string& name, TReadFunction readFunction, TWriteFunction writeFunction)
	{
		registerMemberImpl<TObject,TVarType>(name, std::move(readFunction), std::move(writeFunction));
	}

	template<typename TObject, typename TVarType, typename TReadFunction>
	void registerMemberImpl(tag<TVarType(TObject::*)>, const std::string& name, TReadFunction readFunction)
	{
		registerMemberImpl<TObject, TVarType>(name, std::move(readFunction));
	}

	// the "registerMember" public functions call this one
	template<typename TObject, typename TVarType, typename TReadFunction>
	void registerMemberImpl(TReadFunction readFunction)
	{
		checkTypeRegistration(mState, &typeid(TObject));
		setTable<LUA_REGISTRYINDEX,TVarType (TObject const&, std::string)>(mState, &typeid(TObject), 2, [readFunction](TObject const& object, const std::string& name) {
			return readFunction(object, name);
		});
		
		checkTypeRegistration(mState, &typeid(TObject*));
		setTable<LUA_REGISTRYINDEX,TVarType (TObject*, std::string)>(mState, &typeid(TObject*), 2, [readFunction](TObject const* object, const std::string& name) {
			assert(object);
			return readFunction(*object, name);
		});
		
		checkTypeRegistration(mState, &typeid(TObject const*));
		setTable<LUA_REGISTRYINDEX,TVarType (TObject const*, std::string)>(mState, &typeid(TObject const*), 2, [readFunction](TObject const* object, const std::string& name) {
			assert(object);
			return readFunction(*object, name);
		});
		
		checkTypeRegistration(mState, &typeid(std::shared_ptr<TObject>));
		setTable<LUA_REGISTRYINDEX,TVarType (std::shared_ptr<TObject>, std::string)>(mState, &typeid(std::shared_ptr<TObject>), 2, [readFunction](const std::shared_ptr<TObject>& object, const std::string& name) {
			assert(object);
			return readFunction(*object, name);
		});
		
		checkTypeRegistration(mState, &typeid(std::shared_ptr<TObject const>));
		setTable<LUA_REGISTRYINDEX,TVarType (std::shared_ptr<TObject const>, std::string)>(mState, &typeid(std::shared_ptr<TObject const>), 2, [readFunction](const std::shared_ptr<TObject const>& object, const std::string& name) {
			assert(object);
			return readFunction(*object, name);
		});
	}

	template<typename TObject, typename TVarType, typename TReadFunction, typename TWriteFunction>
	void registerMemberImpl(TReadFunction readFunction, TWriteFunction writeFunction)
	{
		registerMemberImpl<TObject,TVarType>(readFunction);

		setTable<LUA_REGISTRYINDEX, void (TObject&, std::string, TVarType)>(mState, &typeid(TObject), 5, [writeFunction](TObject& object, const std::string& name, const TVarType& value) {
			writeFunction(object, name, value);
		});
		
		setTable<LUA_REGISTRYINDEX, void (TObject*, std::string, TVarType)>(mState, &typeid(TObject*), 2, [writeFunction](TObject* object, const std::string& name, const TVarType& value) {
			assert(object);
			writeFunction(*object, name, value);
		});
		
		setTable<LUA_REGISTRYINDEX, void (std::shared_ptr<TObject>, std::string, TVarType)>(mState, &typeid(std::shared_ptr<TObject>), 2, [writeFunction](const std::shared_ptr<TObject>& object, const std::string& name, const TVarType& value) {
			assert(object);
			writeFunction(*object, name, value);
		});
	}

	template<typename TObject, typename TVarType, typename TReadFunction, typename TWriteFunction>
	void registerMemberImpl(tag<TVarType (TObject::*)>, TReadFunction readFunction, TWriteFunction writeFunction)
	{
		registerMemberImpl<TObject,TVarType>(std::move(readFunction), std::move(writeFunction));
	}

	template<typename TObject, typename TVarType, typename TReadFunction>
	void registerMemberImpl(tag<TVarType(TObject::*)>, TReadFunction readFunction)
	{
		registerMemberImpl<TObject, TVarType>(std::move(readFunction));
	}
	

	/**************************************************/
	/*              LOADING AND CALLING               */
	/**************************************************/
	// this function loads data from the stream and pushes a function at the top of the stack
	// throws in case of syntax error
	static void load(lua_State* state, std::istream& code) {
		// since the lua_load function requires a static function, we use this structure
		// the Reader structure is at the same time an object storing an istream and a buffer,
		//   and a static function provider
		struct Reader {
			Reader(std::istream& str) : stream(str) {}
			std::istream&			stream;
			std::array<char,512>	buffer;

			// read function ; "data" must be an instance of Reader
			static const char* read(lua_State* l, void* data, size_t* size) {
				assert(size != nullptr);
				assert(data != nullptr);
				Reader& me = *static_cast<Reader*>(data);
				if (me.stream.eof())	{ *size = 0; return nullptr; }

				me.stream.read(me.buffer.data(), me.buffer.size());
				*size = static_cast<size_t>(me.stream.gcount());	// gcount could return a value larger than a size_t, but its maximum is me.buffer.size() so there's no problem
				return me.buffer.data();
			}
		};

		// we create an instance of Reader, and we call lua_load
		Reader reader{code};
		const auto loadReturnValue = lua_load(state, &Reader::read, &reader, "chunk"
#			if LUA_VERSION_NUM >= 502
				, nullptr
#			endif
		);

		// now we have to check return value
		if (loadReturnValue != 0) {
			// there was an error during loading, an error message was pushed on the stack
			const std::string errorMsg = lua_tostring(state, -1);
			lua_pop(state, 1);
			if (loadReturnValue == LUA_ERRMEM)
				throw std::bad_alloc();
			else if (loadReturnValue == LUA_ERRSYNTAX)
				throw SyntaxErrorException{errorMsg};
			throw std::runtime_error("Error while calling lua_load: " + errorMsg);
		}
	}
	
	// this function loads data and pushes a function at the top of the stack
	// throws in case of syntax error
	static void load(lua_State* state, const char* code) {
		auto loadReturnValue = luaL_loadstring(state, code);

		// now we have to check return value
		if (loadReturnValue != 0) {
			// there was an error during loading, an error message was pushed on the stack
			const std::string errorMsg = lua_tostring(state, -1);
			lua_pop(state, 1);
			if (loadReturnValue == LUA_ERRMEM)
				throw std::bad_alloc();
			else if (loadReturnValue == LUA_ERRSYNTAX)
				throw SyntaxErrorException{errorMsg};
			throw std::runtime_error("Error while calling lua_load: " + errorMsg);
		}
	}

	// this function calls what is on the top of the stack and removes it (just like lua_call)
	// if an exception is triggered, the top of the stack will be removed anyway
	template<typename TReturnType, typename... TParameters>
	static auto call(lua_State* state, TParameters&&... input)
		-> TReturnType
	{
		typedef typename Tupleizer<TReturnType>::type
			RealReturnType;
		const int outArguments = std::tuple_size<RealReturnType>::value;
		
		// we push the parameters on the stack
		int inArguments;
		try {
			inArguments = Pusher<std::tuple<TParameters...>>::push(state, std::make_tuple(std::forward<TParameters>(input)...));
		} catch(...) {
			lua_pop(state, 1);
			throw;
		}

		// calling pcall automatically pops the parameters and pushes output
		callRaw(state, inArguments, outArguments);

		// pcall succeeded, we pop the returned values and return them
		try {
			return readTopAndPop<TReturnType>(state, outArguments);

		} catch(...) {
			lua_pop(state, outArguments);
			throw;
		}
	}
	
	// this function just calls lua_pcall and checks for errors
	static void callRaw(lua_State* state, const int inArguments, const int outArguments)
	{
		// calling pcall automatically pops the parameters and pushes output
		const auto pcallReturnValue = lua_pcall(state, inArguments, outArguments, 0);

		// if pcall failed, analyzing the problem and throwing
		if (pcallReturnValue != 0) {
			// an error occured during execution, either an error message or a std::exception_ptr was pushed on the stack
			if (pcallReturnValue == LUA_ERRMEM) {
				throw std::bad_alloc{};

			} else if (pcallReturnValue == LUA_ERRRUN) {
				if (lua_isstring(state, 1)) {
					// the error is a string
					const auto str = readTopAndPop<std::string>(state, 1);
					throw ExecutionErrorException{str};

				} else {
					// an exception_ptr was pushed on the stack
					// rethrowing it with an additional ExecutionErrorException
					try {
						std::rethrow_exception(readTopAndPop<std::exception_ptr>(state, 1));
					} catch(...) {
						std::throw_with_nested(ExecutionErrorException{"Exception thrown by a callback function called by Lua"});
					}
				}
			}
		}
	}

	
	/**************************************************/
	/*                PUSH FUNCTIONS                  */
	/**************************************************/
	// the Pusher structures allow you to push a value on the stack
	//  - static const int minSize : minimum size on the stack that the value can have
	//  - static const int maxSize : maximum size on the stack that the value can have
	//  - static int push(const LuaContext&, ValueType) : pushes the value on the stack and returns the size on the stack

	// implementation for custom objects
	template<typename TType, typename = void>
	struct Pusher {
		static const int minSize = 1;
		static const int maxSize = 1;

		template<typename TType2>
		static int push(lua_State* state, TType2&& value) {
			// this function is called when lua's garbage collector wants to destroy our object
			// we simply call its destructor
			const auto garbageCallbackFunction = [](lua_State* lua) -> int {
				assert(lua_gettop(lua) == 1);
				TType* ptr = static_cast<TType*>(lua_touserdata(lua, 1));
				assert(ptr);
				ptr->~TType();
				return 0;
			};

			// this function will be stored in __index in the metatable
			const auto indexFunction = [](lua_State* lua) -> int {
				try {
					assert(lua_gettop(lua) == 2);
					assert(lua_isuserdata(lua, 1));

					// searching for a handler
					lua_pushlightuserdata(lua, const_cast<std::type_info*>(&typeid(TType)));
					lua_gettable(lua, LUA_REGISTRYINDEX);
					assert(!lua_isnil(lua, -1));
					
					// looking into getter functions
					lua_pushinteger(lua, 0);
					lua_gettable(lua, -2);
					lua_pushvalue(lua, 2);
					lua_gettable(lua, -2);
					if (!lua_isnil(lua, -1))
						return 1;
					lua_pop(lua, 2);
					
					// looking into getter members
					lua_pushinteger(lua, 1);
					lua_gettable(lua, -2);
					lua_pushvalue(lua, 2);
					lua_gettable(lua, -2);
					if (!lua_isnil(lua, -1)) {
						lua_pushvalue(lua, 1);
						callRaw(lua, 1, 1);
						return 1;
					}
					lua_pop(lua, 2);

					// looking into default getter
					lua_pushinteger(lua, 2);
					lua_gettable(lua, -2);
					if (lua_isnil(lua, -1))
						return 1;
					lua_pushvalue(lua, 1);
					lua_pushvalue(lua, 2);
					callRaw(lua, 2, 1);
					return 1;

				} catch (...) {
					Pusher<std::exception_ptr>::push(lua, std::current_exception());
					return lua_error(lua);
				}
			};

			// this function will be stored in __newindex in the metatable
			const auto newIndexFunction = [](lua_State* lua) -> int {
				try {
					assert(lua_gettop(lua) == 3);
					assert(lua_isuserdata(lua, 1));

					// searching for a handler
					lua_pushlightuserdata(lua, const_cast<std::type_info*>(&typeid(TType)));
					lua_rawget(lua, LUA_REGISTRYINDEX);
					assert(!lua_isnil(lua, -1));
					
					// looking into setter members
					lua_pushinteger(lua, 4);
					lua_rawget(lua, -2);
					lua_pushvalue(lua, 2);
					lua_rawget(lua, -2);
					if (!lua_isnil(lua, -1)) {
						lua_pushvalue(lua, 1);
						lua_pushvalue(lua, 3);
						callRaw(lua, 2, 0);
						lua_pop(lua, 2);
						return 0;
					}
					lua_pop(lua, 2);

					// looking into default setter
					lua_pushinteger(lua, 5);
					lua_rawget(lua, -2);
					if (lua_isnil(lua, -1))
					{
						lua_pop(lua, 2);
						lua_pushstring(lua, "No setter found");
						return lua_error(lua);
					}
					lua_pushvalue(lua, 1);
					lua_pushvalue(lua, 2);
					lua_pushvalue(lua, 3);
					callRaw(lua, 3, 0);
					lua_pop(lua, 1);
					return 0;

				} catch (...) {
					Pusher<std::exception_ptr>::push(lua, std::current_exception());
					return lua_error(lua);
				}
			};

			// writing structure for this type into the registry
			checkTypeRegistration(state, &typeid(TType));

			// creating the object
			// lua_newuserdata allocates memory in the internals of the lua library and returns it so we can fill it
			//   and that's what we do with placement-new
			const auto pointerLocation = static_cast<TType*>(lua_newuserdata(state, sizeof(TType)));
			new (pointerLocation) TType(std::forward<TType2>(value));

			try {
				// creating the metatable (over the object on the stack)
				// lua_settable pops the key and value we just pushed, so stack management is easy
				// all that remains on the stack after these function calls is the metatable
				lua_newtable(state);
				try {
					// using the garbage collecting function we created above
					if (!boost::has_trivial_destructor<TType>::value)
					{
						lua_pushstring(state, "__gc");
						lua_pushcfunction(state, garbageCallbackFunction);
						lua_settable(state, -3);
					}

					// the _typeid index of the metatable will store the type_info*
					lua_pushstring(state, "_typeid");
					lua_pushlightuserdata(state, const_cast<std::type_info*>(&typeid(TType)));
					lua_settable(state, -3);

					// using the index function we created above
					lua_pushstring(state, "__index");
					lua_pushcfunction(state, indexFunction);
					lua_settable(state, -3);

					// using the newindex function we created above
					lua_pushstring(state, "__newindex");
					lua_pushcfunction(state, newIndexFunction);
					lua_settable(state, -3);

					// at this point, the stack contains the object at offset -2 and the metatable at offset -1
					// lua_setmetatable will bind the two together and pop the metatable
					// our custom type remains on the stack (and that's what we want since this is a push function)
					lua_setmetatable(state, -2);

				} catch (...) { lua_pop(state, 1); throw; }
			} catch (...) { lua_pop(state, 1); throw; }

			return 1;
		}
	};
	
	// this structure has a "size" int static member which is equal to the total of the push min size of all the types
	template<typename... TTypes>
	struct PusherTotalMinSize;

	// this structure has a "size" int static member which is equal to the total of the push max size of all the types
	template<typename... TTypes>
	struct PusherTotalMaxSize;
	
	// this structure has a "size" int static member which is equal to the maximum size of the push of all the types
	template<typename... TTypes>
	struct PusherMinSize;
	
	// this structure has a "size" int static member which is equal to the maximum size of the push of all the types
	template<typename... TTypes>
	struct PusherMaxSize;

	
	/**************************************************/
	/*            CALL FUNCTION WITH TUPLE            */
	/**************************************************/
	// this is a bit of template meta-programming hack
	// the only interesting element here is "callWithTuple" which calls a function using parameters passed as a tuple
	// note that it either returns std::tuple<TrueReturnType> or std::tuple<> instead of void
	template<int...>
	struct Sequence {};
	template<typename Sequence>
	struct IncrementSequence {};
	template<int N>
	struct GenerateSequence { typedef typename IncrementSequence<typename GenerateSequence<N - 1>::type>::type type; };

	template<typename TRetValue, typename TFunctionObject, typename TTuple, int... S>
	static auto callWithTupleImpl(TFunctionObject&& function, const TTuple& parameters, Sequence<S...>)
		-> TRetValue
	{
		return function(std::get<S>(parameters)...);
	}

	template<typename TRetValue, typename TFunctionObject, typename TTuple>
	static auto callWithTuple(TFunctionObject&& function, const TTuple& parameters)
		-> typename std::enable_if<!std::is_void<TRetValue>::value, std::tuple<TRetValue>>::type
	{
		// implementation with a return type
		return std::tuple<TRetValue>(callWithTupleImpl<TRetValue>(std::forward<TFunctionObject>(function), parameters, typename GenerateSequence<std::tuple_size<TTuple>::value>::type()));
	}

	template<typename TRetValue, typename TFunctionObject, typename TTuple>
	static auto callWithTuple(TFunctionObject&& function, const TTuple& parameters)
		-> typename std::enable_if<std::is_void<TRetValue>::value, std::tuple<>>::type
	{
		// implementation without a return type
		callWithTupleImpl<TRetValue>(std::forward<TFunctionObject>(function), parameters, typename GenerateSequence<std::tuple_size<TTuple>::value>::type());
		return std::tuple<>();
	}

	
	/**************************************************/
	/*                READ FUNCTIONS                  */
	/**************************************************/
	// the "Reader" structures allow to read data from the stack
	// - the "ReturnType" type is what is returned by the reader, and can be different than the template parameter (especially with references and constness)
	// - the "test" static function will return true if the variable is of the right type
	// - the "read" static function will assume that the variable is of the right type and read its value
	// - the "testRead" static function will check and read at the same time, returning an empty optional if it is the wrong type
	// - the "readSafe" static function does the same as "testRead" but throws in case of wrong type
	
	template<typename TType, typename = void>
	struct Reader {
		using ReturnType =
			typename std::conditional<std::is_pointer<TType>::value, TType, TType&>::type;
		
		static bool test(lua_State* state, int index)
		{
			if (!lua_isuserdata(state, index))
				return false;
			if (!lua_getmetatable(state, index))
				return false;

			// now we have our metatable on the top of the stack
			// retrieving its _typeid member
			lua_pushstring(state, "_typeid");
			lua_gettable(state, -2);
			const auto storedTypeID = static_cast<const std::type_info*>(lua_touserdata(state, -1));
			const auto typeIDToCompare = &typeid(TType);

			// if wrong typeid, returning false
			lua_pop(state, 2);
			if (storedTypeID != typeIDToCompare)
				return false;

			return true;
		}

		static auto read(lua_State* state, int index)
			-> ReturnType
		{
			return *static_cast<TType*>(lua_touserdata(state, index));
		}

		static auto testRead(lua_State* state, int index)
			-> boost::optional<ReturnType>
		{
			if (!test(state, index))
				return {};
			return read(state, index);
		}

		static auto readSafe(lua_State* state, int index)
			-> ReturnType
		{
			if (!test(state, index))
				throw WrongTypeException{lua_typename(state, lua_type(state, index)), typeid(TType)};
			return read(state, index);
		}
	};


	/**************************************************/
	/*                   UTILITIES                    */
	/**************************************************/
	// structure that will ensure that a certain is stored somewhere in the registry
	struct ValueInRegistry {
		// this constructor will clone and hold the value at the top of the stack in the registry
		ValueInRegistry(lua_State* lua) : lua{lua}
		{
			lua_pushlightuserdata(lua, this);
			lua_pushvalue(lua, -2);
			lua_settable(lua, LUA_REGISTRYINDEX);
		}
		
		// removing the function from the registry
		~ValueInRegistry()
		{
			lua_pushlightuserdata(lua, this);
			lua_pushnil(lua);
			lua_settable(lua, LUA_REGISTRYINDEX);
		}

		// loads the value and puts it at the top of the stack
		void pop()
		{
			lua_pushlightuserdata(lua, this);
			lua_gettable(lua, LUA_REGISTRYINDEX);
		}

		ValueInRegistry(const ValueInRegistry&) = delete;
		ValueInRegistry& operator=(const ValueInRegistry&) = delete;

	private:
		lua_State* lua;
	};

	// turns a type into a tuple
	// void is turned into std::tuple<>
	// existing tuples are untouched
	template<typename T>
	struct Tupleizer;

	// this structure takes a pointer to a member function type and returns the base function type
	template<typename TType>
	struct RemoveMemberPointerFunction { typedef void type; };			// required because of a compiler bug

	// this structure takes any object and detects its function type
	template<typename TObjectType>
	struct FunctionTypeDetector { typedef typename RemoveMemberPointerFunction<decltype(&std::decay<TObjectType>::type::operator())>::type type; };

	// this structure takes a function arguments list and has the "min" and the "max" static const member variables, whose value equal to the min and max number of parameters for the function
	// the only case where "min != max" is with boost::optional at the end of the list
	template<typename... TArgumentsList>
	struct FunctionArgumentsCounter {};
	
	// true is the template parameter is a boost::optional
	template<typename T>
	struct IsOptional : public std::false_type {};
};

/// @deprecated
static LuaContext::EmptyArray_t
	LuaEmptyArray {};
/// @deprecated
static LuaContext::Metatable_t
	LuaMetatable {};
	
/**************************************************/
/*            PARTIAL IMPLEMENTATIONS             */
/**************************************************/
template<>
inline auto LuaContext::readTopAndPop<void>(lua_State* state, int nb)
	-> void
{
	lua_pop(state, nb);
}

// 
template<int... S>
struct LuaContext::IncrementSequence<LuaContext::Sequence<S...>> { typedef Sequence<S..., sizeof...(S)> type; };
template<>
struct LuaContext::GenerateSequence<0> { typedef Sequence<> type; };

// this structure takes a template parameter T
// if T is a tuple, it returns T ; if T is not a tuple, it returns std::tuple<T>
// we have to use this structure because std::tuple<std::tuple<...>> triggers a bug in both MSVC++ and GCC
template<typename T>
struct LuaContext::Tupleizer						{ typedef std::tuple<T>			type; };
template<typename... Args>
struct LuaContext::Tupleizer<std::tuple<Args...>>	{ typedef std::tuple<Args...>	type; };
template<>
struct LuaContext::Tupleizer<void>					{ typedef std::tuple<>			type; };

// this structure takes any object and detects its function type
template<typename TRetValue, typename... TParameters>
struct LuaContext::FunctionTypeDetector<TRetValue (TParameters...)>				{ typedef TRetValue type(TParameters...); };
template<typename TObjectType>
struct LuaContext::FunctionTypeDetector<TObjectType*>							{ typedef typename FunctionTypeDetector<TObjectType>::type type; };

// this structure takes a pointer to a member function type and returns the base function type
template<typename TType, typename TRetValue, typename... TParameters>
struct LuaContext::RemoveMemberPointerFunction<TRetValue (TType::*)(TParameters...)>					{ typedef TRetValue type(TParameters...); };
template<typename TType, typename TRetValue, typename... TParameters>
struct LuaContext::RemoveMemberPointerFunction<TRetValue (TType::*)(TParameters...) const>				{ typedef TRetValue type(TParameters...); };
template<typename TType, typename TRetValue, typename... TParameters>
struct LuaContext::RemoveMemberPointerFunction<TRetValue (TType::*)(TParameters...) volatile>			{ typedef TRetValue type(TParameters...); };
template<typename TType, typename TRetValue, typename... TParameters>
struct LuaContext::RemoveMemberPointerFunction<TRetValue (TType::*)(TParameters...) const volatile>		{ typedef TRetValue type(TParameters...); };

// implementation of PusherTotalMinSize
template<typename TFirst, typename... TTypes>
struct LuaContext::PusherTotalMinSize<TFirst, TTypes...> { static const int size = Pusher<typename std::decay<TFirst>::type>::minSize + PusherTotalMinSize<TTypes...>::size; };
template<>
struct LuaContext::PusherTotalMinSize<> { static const int size = 0; };

// implementation of PusherTotalMaxSize
template<typename TFirst, typename... TTypes>
struct LuaContext::PusherTotalMaxSize<TFirst, TTypes...> { static const int size = Pusher<typename std::decay<TFirst>::type>::maxSize + PusherTotalMaxSize<TTypes...>::size; };
template<>
struct LuaContext::PusherTotalMaxSize<> { static const int size = 0; };

// implementation of PusherMinSize
template<typename TFirst, typename... TTypes>
struct LuaContext::PusherMinSize<TFirst, TTypes...> { static const int size = Pusher<typename std::decay<TFirst>::type>::minSize < PusherTotalMaxSize<TTypes...>::size ? Pusher<typename std::decay<TFirst>::type>::minSize : PusherMinSize<TTypes...>::size; };
template<>
struct LuaContext::PusherMinSize<> { static const int size = 0; };

// implementation of PusherMaxSize
template<typename TFirst, typename... TTypes>
struct LuaContext::PusherMaxSize<TFirst, TTypes...> { static const int size = Pusher<typename std::decay<TFirst>::type>::maxSize > PusherTotalMaxSize<TTypes...>::size ? Pusher<typename std::decay<TFirst>::type>::maxSize : PusherMaxSize<TTypes...>::size; };
template<>
struct LuaContext::PusherMaxSize<> { static const int size = 0; };

// implementation of FunctionArgumentsCounter
template<typename TFirst, typename... TParams>
struct LuaContext::FunctionArgumentsCounter<TFirst, TParams...> {
	typedef FunctionArgumentsCounter<TParams...>
		SubType;
	static const int min = (IsOptional<TFirst>::value && SubType::min == 0) ? 0 : 1 + SubType::min;
	static const int max = 1 + SubType::max;
};
template<>
struct LuaContext::FunctionArgumentsCounter<> {
	static const int min = 0;
	static const int max = 0;
};

// implementation of IsOptional
template<typename T>
struct LuaContext::IsOptional<boost::optional<T>> : public std::true_type {};

// implementation of LuaFunctionCaller
template<typename TFunctionType>
class LuaContext::LuaFunctionCaller { static_assert(std::is_function<TFunctionType>::value, "Template parameter of LuaFunctionCaller must be a function type"); };
template<typename TRetValue, typename... TParams>
class LuaContext::LuaFunctionCaller<TRetValue (TParams...)>
{
public:
	TRetValue operator()(TParams&&... params) const
	{
		valueHolder->pop();
		return call<TRetValue>(state, std::forward<TParams>(params)...);
	}

private:
	std::shared_ptr<ValueInRegistry>	valueHolder;
	lua_State*							state;

private:
	friend LuaContext;
	explicit LuaFunctionCaller(lua_State* state) :
		valueHolder(std::make_shared<ValueInRegistry>(state)),
		state(state)
	{}
};


/**************************************************/
/*                PUSH FUNCTIONS                  */
/**************************************************/
// specializations of the Pusher structure

// boolean
template<>
struct LuaContext::Pusher<bool> {
	static const int minSize = 1;
	static const int maxSize = 1;

	static int push(lua_State* state, bool value) {
		lua_pushboolean(state, value);
		return 1;
	}
};

// string
template<>
struct LuaContext::Pusher<std::string> {
	static const int minSize = 1;
	static const int maxSize = 1;

	static int push(lua_State* state, const std::string& value) {
		lua_pushstring(state, value.c_str());
		return 1;
	}
};

// const char*
template<>
struct LuaContext::Pusher<const char*> {
	static const int minSize = 1;
	static const int maxSize = 1;

	static int push(lua_State* state, const char* value) {
		lua_pushstring(state, value);
		return 1;
	}
};

// const char[N]
template<int N>
struct LuaContext::Pusher<const char[N]> {
	static const int minSize = 1;
	static const int maxSize = 1;

	static int push(lua_State* state, const char* value) {
		lua_pushstring(state, value);
		return 1;
	}
};

// floating numbers
template<typename T>
struct LuaContext::Pusher<T, typename std::enable_if<std::is_floating_point<T>::value>::type> {
	static const int minSize = 1;
	static const int maxSize = 1;

	static int push(lua_State* state, T value) {
		lua_pushnumber(state, value);
		return 1;
	}
};

// integers
template<typename T>
struct LuaContext::Pusher<T, typename std::enable_if<std::is_integral<T>::value>::type> {
	static const int minSize = 1;
	static const int maxSize = 1;

	static int push(lua_State* state, T value) {
		lua_pushinteger(state, value);
		return 1;
	}
};

// nil
template<>
struct LuaContext::Pusher<std::nullptr_t> {
	static const int minSize = 1;
	static const int maxSize = 1;

	static int push(lua_State* state, std::nullptr_t value) {
		assert(value == nullptr);
		lua_pushnil(state);
		return 1;
	}
};

// empty arrays
template<>
struct LuaContext::Pusher<LuaContext::EmptyArray_t> {
	static const int minSize = 1;
	static const int maxSize = 1;

	static int push(lua_State* state, EmptyArray_t) {
		lua_newtable(state);
		return 1;
	}
};

// std::type_info* is a lightuserdata
template<>
struct LuaContext::Pusher<const std::type_info*> {
	static const int minSize = 1;
	static const int maxSize = 1;

	static int push(lua_State* state, const std::type_info* ptr) {
		lua_pushlightuserdata(state, const_cast<std::type_info*>(ptr));
		return 1;
	}
};

// thread
template<>
struct LuaContext::Pusher<LuaContext::ThreadID> {
	static const int minSize = 1;
	static const int maxSize = 1;

	static int push(lua_State* state, const LuaContext::ThreadID& value) {
		lua_pushthread(value.state);
		return 1;
	}
};

// maps
template<typename TKey, typename TValue>
struct LuaContext::Pusher<std::map<TKey,TValue>> {
	static const int minSize = 1;
	static const int maxSize = 1;

	static int push(lua_State* state, const std::map<TKey,TValue>& value) {
		static_assert(Pusher<typename std::decay<TKey>::type>::minSize == 1 && Pusher<typename std::decay<TKey>::type>::maxSize == 1, "Can't push multiple elements for a table key");
		static_assert(Pusher<typename std::decay<TValue>::type>::minSize == 1 && Pusher<typename std::decay<TValue>::type>::maxSize == 1, "Can't push multiple elements for a table value");

		lua_newtable(state);

		for (auto i = value.begin(), e = value.end(); i != e; ++i) {
			Pusher<typename std::decay<TKey>::type>::push(state, i->first);
			Pusher<typename std::decay<TValue>::type>::push(state, i->second);
			lua_settable(state, -3);
		}

		return 1;
	}
};

// unordered_maps
template<typename TKey, typename TValue>
struct LuaContext::Pusher<std::unordered_map<TKey,TValue>> {
	static const int minSize = 1;
	static const int maxSize = 1;

	static int push(lua_State* state, const std::unordered_map<TKey,TValue>& value) {
		static_assert(Pusher<typename std::decay<TKey>::type>::minSize == 1 && Pusher<typename std::decay<TKey>::type>::maxSize == 1, "Can't push multiple elements for a table key");
		static_assert(Pusher<typename std::decay<TValue>::type>::minSize == 1 && Pusher<typename std::decay<TValue>::type>::maxSize == 1, "Can't push multiple elements for a table value");

		lua_newtable(state);

		for (auto i = value.begin(), e = value.end(); i != e; ++i) {
			Pusher<typename std::decay<TKey>::type>::push(state, i->first);
			Pusher<typename std::decay<TValue>::type>::push(state, i->second);
			lua_settable(state, -3);
		}

		return 1;
	}
};

// vectors of pairs
template<typename TType1, typename TType2>
struct LuaContext::Pusher<std::vector<std::pair<TType1,TType2>>> {
	static const int minSize = 1;
	static const int maxSize = 1;

	static int push(lua_State* state, const std::vector<std::pair<TType1,TType2>>& value) {
		static_assert(Pusher<typename std::decay<TType1>::type>::minSize == 1 && Pusher<typename std::decay<TType1>::type>::maxSize == 1, "Can't push multiple elements for a table key");
		static_assert(Pusher<typename std::decay<TType2>::type>::minSize == 1 && Pusher<typename std::decay<TType2>::type>::maxSize == 1, "Can't push multiple elements for a table value");

		lua_newtable(state);

		for (auto i = value.begin(), e = value.end(); i != e; ++i) {
			Pusher<typename std::decay<TType1>::type>::push(state, i->first);
			Pusher<typename std::decay<TType2>::type>::push(state, i->second);
			lua_settable(state, -3);
		}

		return 1;
	}
};

// vectors
template<typename TType>
struct LuaContext::Pusher<std::vector<TType>> {
	static const int minSize = 1;
	static const int maxSize = 1;

	static int push(lua_State* state, const std::vector<TType>& value) {
		static_assert(Pusher<typename std::decay<TType>::type>::minSize == 1 && Pusher<typename std::decay<TType>::type>::maxSize == 1, "Can't push multiple elements for a table value");
		
		lua_newtable(state);

		for (unsigned int i = 0; i < value.size(); ++i) {
			lua_pushinteger(state, i + 1);
			Pusher<typename std::decay<TType>::type>::push(state, value[i]);
			lua_settable(state, -3);
		}

		return 1;
	}
};

// unique_ptr
template<typename TType>
struct LuaContext::Pusher<std::unique_ptr<TType>> {
	static const int minSize = Pusher<std::shared_ptr<TType>>::minSize;
	static const int maxSize = Pusher<std::shared_ptr<TType>>::maxSize;

	static int push(lua_State* state, std::unique_ptr<TType> value) {
		return Pusher<std::shared_ptr<TType>>::push(state, std::move(value));
	}
};

// enum
template<typename TEnum>
struct LuaContext::Pusher<TEnum, typename std::enable_if<std::is_enum<TEnum>::value>::type> {
	#if !defined(__clang__) || __clang_major__ > 3 || (__clang_major__ == 3 && __clang_minor__ > 3)
		typedef typename std::underlying_type<TEnum>::type
			RealType;
	#else
		// implementation when std::underlying_type is not supported
		typedef unsigned long
			RealType;
	#endif

	static const int minSize = Pusher<RealType>::minSize;
	static const int maxSize = Pusher<RealType>::maxSize;

	static int push(lua_State* state, TEnum value) {
		return Pusher<RealType>::push(state, static_cast<RealType>(value));
	}
};

// any function
// this specialization is not directly called, but is called by other specializations
template<typename TReturnType, typename... TParameters>
struct LuaContext::Pusher<TReturnType (TParameters...)>
{
	static const int minSize = 1;
	static const int maxSize = 1;

	// counts the number of arguments
	typedef FunctionArgumentsCounter<TParameters...>
		LocalFunctionArgumentsCounter;

	// this is the version of "push" for non-trivially destructible function objects
	template<typename TFunctionObject>
	static auto push(lua_State* state, TFunctionObject fn)
		-> typename std::enable_if<!boost::has_trivial_destructor<TFunctionObject>::value, int>::type
	{
		// TODO: is_move_constructible not supported by some compilers
		//static_assert(std::is_move_constructible<TFunctionObject>::value, "The function object must be move-constructible");

		// when the lua script calls the thing we will push on the stack, we want "fn" to be executed
		// if we used lua's cfunctions system, we could not detect when the function is no longer in use, which could cause problems
		// so we use userdata instead
		
		// this function is called when the lua script tries to call our custom data type
		// we transfer execution to the "callback" function below
		const auto callCallback = [](lua_State* lua) -> int {
			assert(lua_gettop(lua) >= 1);
			assert(lua_isuserdata(lua, 1));
			auto function = static_cast<TFunctionObject*>(lua_touserdata(lua, 1));
			assert(function);
			return callback(lua, function, lua_gettop(lua) - 1);
		};

		// this one is called when lua's garbage collector no longer needs our custom data type
		// we call the function object's destructor
		const auto garbageCallback = [](lua_State* lua) -> int {
			assert(lua_gettop(lua) == 1);
			auto function = static_cast<TFunctionObject*>(lua_touserdata(lua, 1));
			assert(function);
			function->~TFunctionObject();
			return 0;
		};

		// creating the object
		// lua_newuserdata allocates memory in the internals of the lua library and returns it so we can fill it
		//   and that's what we do with placement-new
		const auto functionLocation = static_cast<TFunctionObject*>(lua_newuserdata(state, sizeof(TFunctionObject)));
		new (functionLocation) TFunctionObject(std::move(fn));

		// creating the metatable (over the object on the stack)
		// lua_settable pops the key and value we just pushed, so stack management is easy
		// all that remains on the stack after these function calls is the metatable
		lua_newtable(state);
		lua_pushstring(state, "__call");
		lua_pushcfunction(state, callCallback);
		lua_settable(state, -3);

		lua_pushstring(state, "__gc");
		lua_pushcfunction(state, garbageCallback);
		lua_settable(state, -3);

		// at this point, the stack contains the object at offset -2 and the metatable at offset -1
		// lua_setmetatable will bind the two together and pop the metatable
		// our custom function remains on the stack (and that's what we want)
		lua_setmetatable(state, -2);

		return 1;
	}

	// this is the version of "push" for trivially destructible objects
	template<typename TFunctionObject>
	static auto push(lua_State* state, TFunctionObject fn)
		-> typename std::enable_if<boost::has_trivial_destructor<TFunctionObject>::value, int>::type
	{
		// TODO: is_move_constructible not supported by some compilers
		//static_assert(std::is_move_constructible<TFunctionObject>::value, "The function object must be move-constructible");

		// when the lua script calls the thing we will push on the stack, we want "fn" to be executed
		// since "fn" doesn't need to be destroyed, we simply push it on the stack

		// this is the cfunction that is the callback
		const auto function = [](lua_State* state) -> int
		{
			// the function object is an upvalue
			const auto toCall = static_cast<TFunctionObject*>(lua_touserdata(state, lua_upvalueindex(1)));
			return callback(state, toCall, lua_gettop(state));
		};

		// we copy the function object onto the stack
		const auto functionObjectLocation = static_cast<TFunctionObject*>(lua_newuserdata(state, sizeof(TFunctionObject)));
		new (functionObjectLocation) TFunctionObject(std::move(fn));

		// pushing the function with the function object as upvalue
		lua_pushcclosure(state, function, 1);
		return 1;
	}

private:
	// callback that calls the function object
	// this function is used by the callbacks and handles loading arguments from the stack and pushing the return value back
	template<typename TFunctionObject>
	static int callback(lua_State* state, TFunctionObject* toCall, int argumentsCount) {
		// checking if number of parameters is correct
		if (argumentsCount < LocalFunctionArgumentsCounter::min) {
			// if not, using lua_error to return an error
			luaL_where(state, 1);
			lua_pushstring(state, "This function requires at least ");
			lua_pushnumber(state, LocalFunctionArgumentsCounter::min);
			lua_pushstring(state, " parameter(s)");
			lua_concat(state, 4);
			return lua_error(state);

		} else if (argumentsCount > LocalFunctionArgumentsCounter::max) {
			// if not, using lua_error to return an error
			luaL_where(state, 1);
			lua_pushstring(state, "This function requires at most ");
			lua_pushnumber(state, LocalFunctionArgumentsCounter::max);
			lua_pushstring(state, " parameter(s)");
			lua_concat(state, 4);
			return lua_error(state);
		}

		std::unique_ptr<typename Reader<std::tuple<TParameters...>>::ReturnType> parameters;
		// reading parameters from the stack
		try {
			parameters.reset(new typename Reader<std::tuple<TParameters...>>::ReturnType(Reader<std::tuple<TParameters...>>::readSafe(state, -argumentsCount, argumentsCount)));

		} catch (const WrongTypeException& ex) {
			// wrong parameter type, using lua_error to return an error
			luaL_where(state, 1);
			lua_pushstring(state, "Unable to convert parameter from ");
			lua_pushstring(state, ex.luaType.c_str());
			lua_pushstring(state, " to ");
			lua_pushstring(state, ex.destination.name());
			lua_concat(state, 4);
			return lua_error(state);
		}
		
		// calling the function, note that "result" should be a tuple
		try {
			auto result = callWithTuple<TReturnType>(*toCall, *parameters);

			// pushing the result on the stack and returning number of pushed elements
			return Pusher<typename std::decay<decltype(result)>::type>::push(state, std::move(result));

		} catch (...) {
			Pusher<std::exception_ptr>::push(state, std::current_exception());
			return lua_error(state);
		}
	}
};

// C function pointers
template<typename TReturnType, typename... TParameters>
struct LuaContext::Pusher<TReturnType (*)(TParameters...)>
{
	// using the function-pushing implementation
	typedef Pusher<TReturnType (TParameters...)>
		SubPusher;
	static const int minSize = SubPusher::minSize;
	static const int maxSize = SubPusher::maxSize;

	template<typename TType>
	static int push(lua_State* state, TType value) {
		return SubPusher::push(state, value);
	}
};

// C function references
template<typename TReturnType, typename... TParameters>
struct LuaContext::Pusher<TReturnType (&)(TParameters...)>
{
	// using the function-pushing implementation
	typedef Pusher<TReturnType(TParameters...)>
		SubPusher;
	static const int minSize = SubPusher::minSize;
	static const int maxSize = SubPusher::maxSize;

	template<typename TType>
	static int push(lua_State* state, TType value) {
		return SubPusher::push(state, value);
	}
};

// std::function
template<typename TReturnType, typename... TParameters>
struct LuaContext::Pusher<std::function<TReturnType (TParameters...)>>
{
	// using the function-pushing implementation
	typedef Pusher<TReturnType (TParameters...)>
		SubPusher;
	static const int minSize = SubPusher::minSize;
	static const int maxSize = SubPusher::maxSize;

	static int push(lua_State* state, const std::function<TReturnType (TParameters...)>& value) {
		return SubPusher::push(state, value);
	}
};

// boost::variant
template<typename... TTypes>
struct LuaContext::Pusher<boost::variant<TTypes...>>
{
	static const int minSize = PusherMinSize<TTypes...>::size;
	static const int maxSize = PusherMaxSize<TTypes...>::size;

	static int push(lua_State* state, const boost::variant<TTypes...>& value) {
		VariantWriter writer{state};
		return value.apply_visitor(writer);
	}

private:
	struct VariantWriter : public boost::static_visitor<int> {
		template<typename TType>
		int operator()(TType value)
		{
			return Pusher<typename std::decay<TType>::type>::push(state, std::move(value));
		}

		VariantWriter(lua_State* state) : state(state) {}
		lua_State* state;
	};
};

// boost::optional
template<typename TType>
struct LuaContext::Pusher<boost::optional<TType>> {
	typedef Pusher<typename std::decay<TType>::type>
		UnderlyingPusher;

	static const int minSize = UnderlyingPusher::minSize < 1 ? UnderlyingPusher::minSize : 1;
	static const int maxSize = UnderlyingPusher::maxSize > 1 ? UnderlyingPusher::maxSize : 1;

	static int push(lua_State* state, const boost::optional<TType>& value) {
		if (value) {
			return UnderlyingPusher::push(state, value.get());
		} else {
			lua_pushnil(state);
			return 1;
		}
	}
};

// tuple
template<typename... TTypes>
struct LuaContext::Pusher<std::tuple<TTypes...>> {
	static const int minSize = PusherTotalMinSize<TTypes...>::size;
	static const int maxSize = PusherTotalMaxSize<TTypes...>::size;

	static int push(lua_State* state, const std::tuple<TTypes...>& value) {
		return push2(state, value, std::integral_constant<int,0>{});
	}

	static int push(lua_State* state, std::tuple<TTypes...>&& value) {
		return push2(state, std::move(value), std::integral_constant<int,0>{});
	}

private:
	template<int N>
	static int push2(lua_State* state, const std::tuple<TTypes...>& value, std::integral_constant<int,N>) {
		typedef typename std::tuple_element<N,std::tuple<TTypes...>>::type ElemType;
		const int pushed = Pusher<typename std::decay<ElemType>::type>::push(state, std::get<N>(value));
		try {
			return pushed + push2(state, value, std::integral_constant<int,N+1>{});
		} catch(...) {
			lua_pop(state, pushed);
			throw;
		}
	}

	template<int N>
	static int push2(lua_State* state, std::tuple<TTypes...>&& value, std::integral_constant<int,N>) {
		typedef typename std::tuple_element<N,std::tuple<TTypes...>>::type ElemType;
		const int pushed = Pusher<typename std::decay<ElemType>::type>::push(state, std::move(std::get<N>(value)));
		try {
			return pushed + push2(state, std::move(value), std::integral_constant<int,N+1>{});
		} catch(...) {
			lua_pop(state, pushed);
			throw;
		}
	}
	
	static int push2(lua_State* state, const std::tuple<TTypes...>&, std::integral_constant<int,sizeof...(TTypes)>) {
		return 0;
	}
	
	static int push2(lua_State* state, std::tuple<TTypes...>&&, std::integral_constant<int,sizeof...(TTypes)>) {
		return 0;
	}
};

/**************************************************/
/*                READ FUNCTIONS                  */
/**************************************************/
// specializations of the Reader structures

// reading null
template<>
struct LuaContext::Reader<std::nullptr_t>
{
	using ReturnType = std::nullptr_t;

	static bool test(lua_State* state, int index)
	{
		return lua_isnil(state, index);
	}
	
	static auto read(lua_State* state, int index)
		-> std::nullptr_t
	{
		return nullptr;
	}

	static auto testRead(lua_State* state, int index)
		-> boost::optional<std::nullptr_t>
	{
		if (!test(state, index))
			return {};
		return nullptr;
	}

	static auto readSafe(lua_State* state, int index)
		-> std::nullptr_t
	{
		if (!test(state, index))
			throw WrongTypeException{lua_typename(state, lua_type(state, index)), typeid(std::nullptr_t)};
		return nullptr;
	}
};

// integrals
template<typename TType>
struct LuaContext::Reader<
			TType,
			typename std::enable_if<std::is_integral<TType>::value>::type
		>
{
	using ReturnType = TType;

	static bool test(lua_State* state, int index)
	{
		return lua_isnumber(state, index) && fmod(lua_tonumber(state, index), 1.) == 0;
	}
	
	static auto read(lua_State* state, int index)
		-> TType
	{
		return lua_tointeger(state, index);
	}

	static auto testRead(lua_State* state, int index)
		-> boost::optional<TType>
	{
		if (!lua_isnumber(state, index))
			return {};
		const auto nb = lua_tonumber(state, index);
		if (fmod(nb, 1.f) != 0)
			return {};
		return static_cast<TType>(nb);
	}

	static auto readSafe(lua_State* state, int index)
		-> TType
	{
		const auto val = testRead(state, index);
		if (!val)
			throw WrongTypeException{lua_typename(state, lua_type(state, index)), typeid(TType)};
		return val.get();
	}
};

// floating points
template<typename TType>
struct LuaContext::Reader<
			TType,
			typename std::enable_if<std::is_floating_point<TType>::value>::type
		>
{
	using ReturnType = TType;

	static bool test(lua_State* state, int index)
	{
		return lua_isnumber(state, index) != 0;
	}
	
	static auto read(lua_State* state, int index)
		-> TType
	{
		return static_cast<TType>(lua_tonumber(state, index));
	}

	static auto testRead(lua_State* state, int index)
		-> boost::optional<TType>
	{
		if (!test(state, index))
			return {};
		return read(state, index);
	}

	static auto readSafe(lua_State* state, int index)
		-> TType
	{
		if (!test(state, index))
			throw WrongTypeException{lua_typename(state, lua_type(state, index)), typeid(TType)};
		return read(state, index);
	}
};

// boolean
template<>
struct LuaContext::Reader<bool>
{
	using ReturnType = bool;

	static bool test(lua_State* state, int index)
	{
		return lua_isboolean(state, index);
	}
	
	static auto read(lua_State* state, int index)
		-> bool
	{
		return lua_toboolean(state, index) != 0;
	}

	static auto testRead(lua_State* state, int index)
		-> boost::optional<bool>
	{
		if (!test(state, index))
			return {};
		return read(state, index);
	}

	static auto readSafe(lua_State* state, int index)
		-> bool
	{
		if (!test(state, index))
			throw WrongTypeException{lua_typename(state, lua_type(state, index)), typeid(bool)};
		return read(state, index);
	}
};

// string
// lua_tostring returns a temporary pointer, but that's not a problem since we copy
//   the data into a std::string
template<>
struct LuaContext::Reader<std::string>
{
	using ReturnType = std::string;

	static bool test(lua_State* state, int index)
	{
		return lua_isstring(state, index) != 0;
	}
	
	static auto read(lua_State* state, int index)
		-> std::string
	{
		return lua_tostring(state, index);
	}

	static auto testRead(lua_State* state, int index)
		-> boost::optional<std::string>
	{
		const auto val = lua_tostring(state, index);
		if (val == 0)
			return {};
		return std::string(val);
	}

	static auto readSafe(lua_State* state, int index)
		-> std::string
	{
		const auto val = lua_tostring(state, index);
		if (val == 0)
			throw WrongTypeException{lua_typename(state, lua_type(state, index)), typeid(std::string)};
		return std::string(val);
	}
};

// enums
template<typename TType>
struct LuaContext::Reader<
			TType,
			typename std::enable_if<std::is_enum<TType>::value>::type
		>
{
	using ReturnType = TType;

	static bool test(lua_State* state, int index)
	{
		return lua_isnumber(state, index) != 0 && fmod(lua_tonumber(state, index), 1.) == 0;
	}
	
	static auto read(lua_State* state, int index)
		-> TType
	{
		return static_cast<TType>(lua_tointeger(state, index));
	}

	static auto testRead(lua_State* state, int index)
		-> boost::optional<TType>
	{
		if (!test(state, index))
			return {};
		return read(state, index);
	}

	static auto readSafe(lua_State* state, int index)
		-> TType
	{
		if (!test(state, index))
			throw WrongTypeException{lua_typename(state, lua_type(state, index)), typeid(TType)};
		return read(state, index);
	}
};

// LuaFunctionCaller
template<typename TRetValue, typename... TParameters>
struct LuaContext::Reader<LuaContext::LuaFunctionCaller<TRetValue (TParameters...)>>
{
	typedef LuaFunctionCaller<TRetValue (TParameters...)>
		ReturnType;

	static bool test(lua_State* state, int index)
	{
		return lua_isfunction(state, index) != 0;
	}
	
	static auto read(lua_State* state, int index)
		-> ReturnType
	{
		return ReturnType(state);
	}

	static auto testRead(lua_State* state, int index)
		-> boost::optional<ReturnType>
	{
		if (!test(state, index))
			return {};
		return read(state, index);
	}

	static auto readSafe(lua_State* state, int index)
		-> ReturnType
	{
		if (!test(state, index))
			throw WrongTypeException{lua_typename(state, lua_type(state, index)), typeid(ReturnType)};
		return read(state, index);
	}
};

// function
template<typename TRetValue, typename... TParameters>
struct LuaContext::Reader<std::function<TRetValue (TParameters...)>>
{
	using SubReader =
		Reader<LuaContext::LuaFunctionCaller<TRetValue (TParameters...)>>;
	using ReturnType =
		typename SubReader::ReturnType;

	static bool test(lua_State* state, int index)
	{
		return SubReader::test(state, index);
	}
	
	static auto read(lua_State* state, int index)
		-> ReturnType
	{
		return SubReader::read(state, index);
	}

	static auto testRead(lua_State* state, int index)
		-> boost::optional<ReturnType>
	{
		return SubReader::testRead(state, index);
	}

	static auto readSafe(lua_State* state, int index)
		-> ReturnType
	{
		return SubReader::readSafe(state, index);
	}
};

// vector of pairs
template<typename TType1, typename TType2>
struct LuaContext::Reader<std::vector<std::pair<TType1,TType2>>>
{
	typedef Reader<typename std::decay<TType1>::type>
		Type1Reader;
	typedef Reader<typename std::decay<TType2>::type>
		Type2Reader;
	using ReturnType =
		std::vector<std::pair<typename Type1Reader::ReturnType, typename Type2Reader::ReturnType>>;

	static bool test(lua_State* state, int index)
	{
		return lua_istable(state, index);
	}
	
	static auto read(lua_State* state, int index)
		-> ReturnType
	{
		return readSafe(state, index);
	}

	static auto testRead(lua_State* state, int index)
		-> boost::optional<ReturnType>
	{
		if (!test(state, index))
			return {};

		ReturnType result;

		// we traverse the table at the top of the stack
		lua_pushnil(state);		// first key
		while (lua_next(state, (index > 0) ? index : (index - 1)) != 0) {
			// now a key and its value are pushed on the stack
			try {
				auto val1 = Type1Reader::testRead(state, -2);
				auto val2 = Type2Reader::testRead(state, -1);

				if (!val1.is_initialized() || !val2.is_initialized()) {
					lua_pop(state, 2);		// we remove the value and the key
					return {};
				}

				result.push_back({ std::move(val1.get()), std::move(val2.get()) });
				lua_pop(state, 1);		// we remove the value but keep the key for the next iteration

			} catch(...) {
				lua_pop(state, 2);		// we remove the value and the key
				return {};
			}
		}

		return { std::move(result) };
	}

	static auto readSafe(lua_State* state, int index)
		-> ReturnType
	{
		if (!lua_istable(state, index))
			throw WrongTypeException{lua_typename(state, lua_type(state, index)), typeid(ReturnType)};

		ReturnType result;

		// we traverse the table at the top of the stack
		lua_pushnil(state);		// first key
		while (lua_next(state, (index > 0) ? index : (index - 1)) != 0) {
			// now a key and its value are pushed on the stack
			try {
				auto val1 = Type1Reader::readSafe(state, -2);
				auto val2 = Type2Reader::readSafe(state, -1);
				
				result.push_back({ std::move(val1), std::move(val2) });
				lua_pop(state, 1);		// we remove the value but keep the key for the next iteration

			} catch(...) {
				lua_pop(state, 2);		// we remove the value and the key
				throw;
			}
		}

		return std::move(result);
	}
};

// map
template<typename TKey, typename TValue>
struct LuaContext::Reader<std::map<TKey,TValue>>
{
	typedef Reader<typename std::decay<TKey>::type>
		KeyReader;
	typedef Reader<typename std::decay<TValue>::type>
		ValueReader;
	using ReturnType =
		std::map<typename KeyReader::ReturnType, typename ValueReader::ReturnType>;

	static bool test(lua_State* state, int index)
	{
		return lua_istable(state, index);
	}
	
	static auto read(lua_State* state, int index)
		-> ReturnType
	{
		return readSafe(state, index);
	}

	static auto testRead(lua_State* state, int index)
		-> boost::optional<ReturnType>
	{
		if (!test(state, index))
			return {};

		ReturnType result;

		// we traverse the table at the top of the stack
		lua_pushnil(state);		// first key
		while (lua_next(state, (index > 0) ? index : (index - 1)) != 0) {
			// now a key and its value are pushed on the stack
			try {
				auto key = KeyReader::testRead(state, -2);
				auto value = ValueReader::testRead(state, -1);

				if (!key.is_initialized() || !value.is_initialized()) {
					lua_pop(state, 2);		// we remove the value and the key
					return {};
				}

				result.insert({ std::move(key.get()), std::move(value.get()) });
				lua_pop(state, 1);		// we remove the value but keep the key for the next iteration

			} catch(...) {
				lua_pop(state, 2);		// we remove the value and the key
				return {};
			}
		}

		return { std::move(result) };
	}

	static auto readSafe(lua_State* state, int index)
		-> ReturnType
	{
		if (!lua_istable(state, index))
			throw WrongTypeException{lua_typename(state, lua_type(state, index)), typeid(ReturnType)};

		ReturnType result;

		// we traverse the table at the top of the stack
		lua_pushnil(state);		// first key
		while (lua_next(state, (index > 0) ? index : (index - 1)) != 0) {
			// now a key and its value are pushed on the stack
			try {
				auto key = KeyReader::readSafe(state, -2);
				auto value = ValueReader::readSafe(state, -1);
				
				result.insert({ std::move(key), std::move(value) });
				lua_pop(state, 1);		// we remove the value but keep the key for the next iteration

			} catch(...) {
				lua_pop(state, 2);		// we remove the value and the key
				throw;
			}
		}

		return std::move(result);
	}
};

// unordered_map
template<typename TKey, typename TValue>
struct LuaContext::Reader<std::unordered_map<TKey,TValue>>
{
	typedef Reader<typename std::decay<TKey>::type>
		KeyReader;
	typedef Reader<typename std::decay<TValue>::type>
		ValueReader;
	using ReturnType =
		std::unordered_map<typename KeyReader::ReturnType, typename ValueReader::ReturnType>;

	static bool test(lua_State* state, int index)
	{
		return lua_istable(state, index);
	}
	
	static auto read(lua_State* state, int index)
		-> ReturnType
	{
		return readSafe(state, index);
	}

	static auto testRead(lua_State* state, int index)
		-> boost::optional<ReturnType>
	{
		if (!test(state, index))
			return {};

		ReturnType result;

		// we traverse the table at the top of the stack
		lua_pushnil(state);		// first key
		while (lua_next(state, (index > 0) ? index : (index - 1)) != 0) {
			// now a key and its value are pushed on the stack
			try {
				auto key = KeyReader::testRead(state, -2);
				auto value = ValueReader::testRead(state, -1);

				if (!key.is_initialized() || !value.is_initialized()) {
					lua_pop(state, 2);		// we remove the value and the key
					return {};
				}

				result.insert({ std::move(key.get()), std::move(value.get()) });
				lua_pop(state, 1);		// we remove the value but keep the key for the next iteration

			} catch(...) {
				lua_pop(state, 2);		// we remove the value and the key
				return {};
			}
		}

		return { std::move(result) };
	}

	static auto readSafe(lua_State* state, int index)
		-> ReturnType
	{
		ReturnType result;

		if (!lua_istable(state, index))
			throw WrongTypeException{lua_typename(state, lua_type(state, index)), typeid(ReturnType)};

		// we traverse the table at the top of the stack
		lua_pushnil(state);		// first key
		while (lua_next(state, (index > 0) ? index : (index - 1)) != 0) {
			// now a key and its value are pushed on the stack
			try {
				auto key = KeyReader::readSafe(state, -2);
				auto value = ValueReader::readSafe(state, -1);
				
				result.insert({ std::move(key.get()), std::move(value.get()) });
				lua_pop(state, 1);		// we remove the value but keep the key for the next iteration

			} catch(...) {
				lua_pop(state, 2);		// we remove the value and the key
				throw;
			}
		}

		return std::move(result);
	}
};

// optional
template<typename TType>
struct LuaContext::Reader<boost::optional<TType>>
{
	typedef Reader<typename std::decay<TType>::type>
		SubReader;
	using ReturnType =
		boost::optional<typename SubReader::ReturnType>;

	static bool test(lua_State* state, int index)
	{
		return lua_isnil(state, index) || SubReader::test(state, index);
	}
	
	static auto read(lua_State* state, int index)
		-> ReturnType
	{
		return lua_isnil(state, index) ? ReturnType{} : ReturnType{SubReader::read(state, index)};
	}

	static auto testRead(lua_State* state, int index)
		-> boost::optional<ReturnType>
	{
		return lua_isnil(state, index) ? ReturnType{} : ReturnType{SubReader::testRead(state, index)};
	}

	static auto readSafe(lua_State* state, int index)
		-> ReturnType
	{
		return lua_isnil(state, index) ? ReturnType{} : ReturnType{SubReader::readSafe(state, index)};
	}
};

// variant
template<typename... TTypes>
struct LuaContext::Reader<boost::variant<TTypes...>>
{
private:
	// this type satisfies the "MPL metaclass" requirement, and can transform a list of types into what the Reader would read from them
	struct ReturnTypeApplier
	{
		template<typename T>
		struct apply
		{
			typedef typename Reader<typename std::decay<T>::type>::ReturnType
				type;
		};
	};

public:
	typedef boost::variant<TTypes...>
		RawVariant;
	typedef typename boost::make_variant_over<typename boost::mpl::transform<typename RawVariant::types, ReturnTypeApplier>::type>::type
		ReturnType;
	
private:
	// class doing operations for a range of types from TIterBegin to TIterEnd
	template<typename TIterBegin, typename TIterEnd, typename = void>
	struct VariantReader
	{
		using SubReader = Reader<typename std::decay<typename boost::mpl::deref<TIterBegin>::type>::type>;

		static auto readSafe(lua_State* state, int index)
			-> ReturnType
		{
			// note: using SubReader::testRead triggers a compilation error when used with a reference
			if (SubReader::test(state, index))
				return ReturnType{SubReader::read(state, index)};
			return VariantReader<typename boost::mpl::next<TIterBegin>::type, TIterEnd>::readSafe(state, index);
		}

		static auto testRead(lua_State* state, int index)
			-> boost::optional<ReturnType>
		{
			// note: using SubReader::testRead triggers a compilation error when used with a reference
			if (SubReader::test(state, index))
				return ReturnType{SubReader::read(state, index)};
			return VariantReader<typename boost::mpl::next<TIterBegin>::type, TIterEnd>::testRead(state, index);
		}
		
		static bool test(lua_State* state, int index)
		{
			if (SubReader::test(state, index))
				return true;
			return VariantReader<typename boost::mpl::next<TIterBegin>::type, TIterEnd>::test(state, index);
		}
	};

	// specialization of class above being called when list of remaining types is empty
	template<typename TIterBegin, typename TIterEnd>
	struct VariantReader<TIterBegin, TIterEnd, typename std::enable_if<boost::mpl::distance<TIterBegin, TIterEnd>::type::value == 0>::type>
	{
		static auto readSafe(lua_State* state, int index)
			-> ReturnType
		{
			throw WrongTypeException(lua_typename(state, lua_type(state, index)), typeid(ReturnType));
		}

		static auto testRead(lua_State* state, int index)
			-> boost::optional<ReturnType> 
		{
			return {};
		}
		
		static bool test(lua_State* state, int index)
		{
			return false;
		}
	};

	// this is the main type
	typedef VariantReader<typename boost::mpl::begin<typename RawVariant::types>::type, typename boost::mpl::end<typename RawVariant::types>::type>
		MainVariantReader;

public:
	static bool test(lua_State* state, int index)
	{
		return MainVariantReader::test(state, index);
	}
	
	static auto read(lua_State* state, int index)
		-> ReturnType
	{
		return readSafe(state, index);
	}

	static auto testRead(lua_State* state, int index)
		-> boost::optional<ReturnType>
	{
		return MainVariantReader::testRead(state, index);
	}

	static auto readSafe(lua_State* state, int index)
		-> ReturnType
	{
		return MainVariantReader::readSafe(state, index);
	}
};

// reading a tuple
// tuple have an additional argument for their functions, that is the maximum size to read
// if maxSize is smaller than the tuple size, then the remaining parameters will be left to default value
template<>
struct LuaContext::Reader<std::tuple<>>
{
	using ReturnType = std::tuple<>;

	static bool test(lua_State* state, int index, int maxSize = 0)
	{
		return true;
	}
	
	static auto read(lua_State* state, int index, int maxSize = 0)
		-> std::tuple<>
	{
		return {};
	}

	static auto testRead(lua_State* state, int index, int maxSize = 0)
		-> boost::optional<std::tuple<>>
	{
		return std::tuple<>{};
	}

	static auto readSafe(lua_State* state, int index, int maxSize = 0)
		-> std::tuple<>
	{
		return {};
	}
};

template<typename TFirst, typename... TOthers>
struct LuaContext::Reader<std::tuple<TFirst, TOthers...>,
		typename std::enable_if<!LuaContext::IsOptional<TFirst>::value>::type		// TODO: replace by std::is_default_constructible when it works on every compiler
	>
{
	// this is the "TFirst is NOT default constructible" version

	typedef Reader<typename std::decay<TFirst>::type>
		TFirstReader;
	typedef Reader<std::tuple<TOthers...>>
		TOthersReader;
	typedef decltype(std::tuple_cat(std::declval<std::tuple<typename TFirstReader::ReturnType>>(), std::declval<typename TOthersReader::ReturnType>()))
		ReturnType;

	static bool test(lua_State* state, int index, int maxSize = std::tuple_size<ReturnType>::value)
	{
		if (maxSize <= 0)
			return false;
		return TFirstReader::test(state, index) && TOthersReader::test(state, index + 1, maxSize - 1);
	}
	
	static auto read(lua_State* state, int index, int maxSize = std::tuple_size<ReturnType>::value)
		-> ReturnType
	{
		if (maxSize <= 0)
			throw WrongTypeException{"null", typeid(std::tuple<TFirst,TOthers...>)};
		return std::tuple_cat(std::tuple<typename TFirstReader::ReturnType>(TFirstReader::read(state, index)), TOthersReader::read(state, index + 1, maxSize - 1));
	}

	static auto testRead(lua_State* state, int index, int maxSize = std::tuple_size<ReturnType>::value)
		-> boost::optional<ReturnType>
	{
		if (!test(state, index))
			return {};
		return read(state, index);
	}

	static auto readSafe(lua_State* state, int index, int maxSize = std::tuple_size<ReturnType>::value)
		-> ReturnType
	{
		try {
			if (maxSize <= 0)
				throw std::logic_error("Trying to read null into non-default-constructible type");
			return std::tuple_cat(std::tuple<typename TFirstReader::ReturnType>(TFirstReader::readSafe(state, index)), TOthersReader::readSafe(state, index + 1, maxSize - 1));

		} catch(...) {
			std::throw_with_nested(WrongTypeException{"unknown", typeid(std::tuple<TFirst,TOthers...>)});
		}
	}
};

template<typename TFirst, typename... TOthers>
struct LuaContext::Reader<std::tuple<TFirst, TOthers...>,
		typename std::enable_if<LuaContext::IsOptional<TFirst>::value>::type		// TODO: replace by std::is_default_constructible when it works on every compiler
	>
{
	// this is the "TFirst is default-constructible" version
	
	typedef Reader<typename std::decay<TFirst>::type>
		TFirstReader;
	typedef Reader<std::tuple<TOthers...>>
		TOthersReader;
	typedef decltype(std::tuple_cat(std::declval<std::tuple<typename TFirstReader::ReturnType>>(), std::declval<typename TOthersReader::ReturnType>()))
		ReturnType;

	static bool test(lua_State* state, int index, int maxSize = std::tuple_size<ReturnType>::value)
	{
		if (maxSize <= 0)
			return true;
		return TFirstReader::test(state, index) && TOthersReader::test(state, index + 1, maxSize - 1);
	}
	
	static auto read(lua_State* state, int index, int maxSize = std::tuple_size<ReturnType>::value)
		-> ReturnType
	{
		if (maxSize <= 0)
			return std::tuple_cat(std::tuple<typename TFirstReader::ReturnType>(), TOthersReader::read(state, index + 1, maxSize - 1));
		return std::tuple_cat(std::tuple<typename TFirstReader::ReturnType>(TFirstReader::read(state, index)), TOthersReader::read(state, index + 1, maxSize - 1));
	}

	static auto testRead(lua_State* state, int index, int maxSize = std::tuple_size<ReturnType>::value)
		-> boost::optional<ReturnType>
	{
		if (!test(state, index))
			return {};
		return read(state, index);
	}

	static auto readSafe(lua_State* state, int index, int maxSize = std::tuple_size<ReturnType>::value)
		-> ReturnType
	{
		try {
			if (maxSize <= 0)
				return std::tuple_cat(std::tuple<typename TFirstReader::ReturnType>(), TOthersReader::readSafe(state, index + 1, maxSize - 1));
			return std::tuple_cat(std::tuple<typename TFirstReader::ReturnType>(TFirstReader::readSafe(state, index)), TOthersReader::readSafe(state, index + 1, maxSize - 1));

		} catch(...) {
			std::throw_with_nested(WrongTypeException{"unknown", typeid(std::tuple<TFirst,TOthers...>)});
		}
	}
};

#endif
