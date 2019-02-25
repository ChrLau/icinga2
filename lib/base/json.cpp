/* Icinga 2 | (c) 2012 Icinga GmbH | GPLv2+ */

#include "base/json.hpp"
#include "base/debug.hpp"
#include "base/namespace.hpp"
#include "base/dictionary.hpp"
#include "base/array.hpp"
#include "base/objectlock.hpp"
#include "base/convert.hpp"
#include <boost/exception_ptr.hpp>
#include <yajl/yajl_version.h>
#include <yajl/yajl_gen.h>
#include <yajl/yajl_parse.h>
#include <stack>

using namespace icinga;

static void Encode(yajl_gen handle, const Value& value);

#if YAJL_MAJOR < 2
typedef unsigned int yajl_size;
#else /* YAJL_MAJOR */
typedef size_t yajl_size;
#endif /* YAJL_MAJOR */

static void EncodeNamespace(yajl_gen handle, const Namespace::Ptr& ns)
{
	yajl_gen_map_open(handle);

	ObjectLock olock(ns);
	for (const Namespace::Pair& kv : ns) {
		yajl_gen_string(handle, reinterpret_cast<const unsigned char *>(kv.first.CStr()), kv.first.GetLength());
		Encode(handle, kv.second->Get());
	}

	yajl_gen_map_close(handle);
}

static void EncodeDictionary(yajl_gen handle, const Dictionary::Ptr& dict)
{
	yajl_gen_map_open(handle);

	ObjectLock olock(dict);
	for (const Dictionary::Pair& kv : dict) {
		yajl_gen_string(handle, reinterpret_cast<const unsigned char *>(kv.first.CStr()), kv.first.GetLength());
		Encode(handle, kv.second);
	}

	yajl_gen_map_close(handle);
}

static void EncodeArray(yajl_gen handle, const Array::Ptr& arr)
{
	yajl_gen_array_open(handle);

	ObjectLock olock(arr);
	for (const Value& value : arr) {
		Encode(handle, value);
	}

	yajl_gen_array_close(handle);
}

static void Encode(yajl_gen handle, const Value& value)
{
	switch (value.GetType()) {
		case ValueNumber:
			if (yajl_gen_double(handle, value.Get<double>()) == yajl_gen_invalid_number)
				yajl_gen_double(handle, 0);

			break;
		case ValueBoolean:
			yajl_gen_bool(handle, value.ToBool());

			break;
		case ValueString:
			yajl_gen_string(handle, reinterpret_cast<const unsigned char *>(value.Get<String>().CStr()), value.Get<String>().GetLength());

			break;
		case ValueObject:
			{
				const Object::Ptr& obj = value.Get<Object::Ptr>();
				Namespace::Ptr ns = dynamic_pointer_cast<Namespace>(obj);

				if (ns) {
					EncodeNamespace(handle, ns);
					break;
				}

				Dictionary::Ptr dict = dynamic_pointer_cast<Dictionary>(obj);

				if (dict) {
					EncodeDictionary(handle, dict);
					break;
				}

				Array::Ptr arr = dynamic_pointer_cast<Array>(obj);

				if (arr) {
					EncodeArray(handle, arr);
					break;
				}
			}

			yajl_gen_null(handle);

			break;
		case ValueEmpty:
			yajl_gen_null(handle);

			break;
		default:
			VERIFY(!"Invalid variant type.");
	}
}

String icinga::JsonEncode(const Value& value, bool pretty_print)
{
#if YAJL_MAJOR < 2
	yajl_gen_config conf = { pretty_print, "" };
	yajl_gen handle = yajl_gen_alloc(&conf, nullptr);
#else /* YAJL_MAJOR */
	yajl_gen handle = yajl_gen_alloc(nullptr);
	if (pretty_print)
		yajl_gen_config(handle, yajl_gen_beautify, 1);
#endif /* YAJL_MAJOR */

	Encode(handle, value);

	const unsigned char *buf;
	yajl_size len;

	yajl_gen_get_buf(handle, &buf, &len);

	String result = String(buf, buf + len);

	yajl_gen_free(handle);

	return result;
}

struct JsonElement
{
	String Key;
	bool KeySet{false};
	Value EValue;
};

struct JsonContext
{
public:
	void Push(const Value& value)
	{
		JsonElement element;
		element.EValue = value;

		m_Stack.push(element);
	}

	JsonElement Pop()
	{
		JsonElement value = m_Stack.top();
		m_Stack.pop();
		return value;
	}

	void AddValue(const Value& value)
	{
		if (m_Stack.empty()) {
			JsonElement element;
			element.EValue = value;
			m_Stack.push(element);
			return;
		}

		JsonElement& element = m_Stack.top();

		if (element.EValue.IsObjectType<Dictionary>()) {
			if (!element.KeySet) {
				element.Key = value;
				element.KeySet = true;
			} else {
				Dictionary::Ptr dict = element.EValue;
				dict->Set(element.Key, value);
				element.KeySet = false;
			}
		} else if (element.EValue.IsObjectType<Array>()) {
			Array::Ptr arr = element.EValue;
			arr->Add(value);
		} else {
			BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot add value to JSON element."));
		}
	}

	Value GetValue() const
	{
		ASSERT(m_Stack.size() == 1);
		return m_Stack.top().EValue;
	}

	void SaveException()
	{
		m_Exception = boost::current_exception();
	}

	void ThrowException() const
	{
		if (m_Exception)
			boost::rethrow_exception(m_Exception);
	}

private:
	std::stack<JsonElement> m_Stack;
	Value m_Key;
	boost::exception_ptr m_Exception;
};

static int DecodeNull(void *ctx)
{
	auto *context = static_cast<JsonContext *>(ctx);

	try {
		context->AddValue(Empty);
	} catch (...) {
		context->SaveException();
		return 0;
	}

	return 1;
}

static int DecodeBoolean(void *ctx, int value)
{
	auto *context = static_cast<JsonContext *>(ctx);

	try {
		context->AddValue(static_cast<bool>(value));
	} catch (...) {
		context->SaveException();
		return 0;
	}

	return 1;
}

static int DecodeNumber(void *ctx, const char *str, yajl_size len)
{
	auto *context = static_cast<JsonContext *>(ctx);

	try {
		String jstr = String(str, str + len);
		context->AddValue(Convert::ToDouble(jstr));
	} catch (...) {
		context->SaveException();
		return 0;
	}

	return 1;
}

static int DecodeString(void *ctx, const unsigned char *str, yajl_size len)
{
	auto *context = static_cast<JsonContext *>(ctx);

	try {
		context->AddValue(String(str, str + len));
	} catch (...) {
		context->SaveException();
		return 0;
	}

	return 1;
}

static int DecodeStartMap(void *ctx)
{
	auto *context = static_cast<JsonContext *>(ctx);

	try {
		context->Push(new Dictionary());
	} catch (...) {
		context->SaveException();
		return 0;
	}

	return 1;
}

static int DecodeEndMapOrArray(void *ctx)
{
	auto *context = static_cast<JsonContext *>(ctx);

	try {
		context->AddValue(context->Pop().EValue);
	} catch (...) {
		context->SaveException();
		return 0;
	}

	return 1;
}

static int DecodeStartArray(void *ctx)
{
	auto *context = static_cast<JsonContext *>(ctx);

	try {
		context->Push(new Array());
	} catch (...) {
		context->SaveException();
		return 0;
	}

	return 1;
}

Value icinga::JsonDecode(const String& data)
{
	static const yajl_callbacks callbacks = {
		DecodeNull,
		DecodeBoolean,
		nullptr,
		nullptr,
		DecodeNumber,
		DecodeString,
		DecodeStartMap,
		DecodeString,
		DecodeEndMapOrArray,
		DecodeStartArray,
		DecodeEndMapOrArray
	};

	yajl_handle handle;
#if YAJL_MAJOR < 2
	yajl_parser_config cfg = { 1, 0 };
#endif /* YAJL_MAJOR */
	JsonContext context;

#if YAJL_MAJOR < 2
	handle = yajl_alloc(&callbacks, &cfg, nullptr, &context);
#else /* YAJL_MAJOR */
	handle = yajl_alloc(&callbacks, nullptr, &context);
	yajl_config(handle, yajl_dont_validate_strings, 1);
	yajl_config(handle, yajl_allow_comments, 1);
#endif /* YAJL_MAJOR */

	yajl_parse(handle, reinterpret_cast<const unsigned char *>(data.CStr()), data.GetLength());

#if YAJL_MAJOR < 2
	if (yajl_parse_complete(handle) != yajl_status_ok) {
#else /* YAJL_MAJOR */
	if (yajl_complete_parse(handle) != yajl_status_ok) {
#endif /* YAJL_MAJOR */
		unsigned char *internal_err_str = yajl_get_error(handle, 1, reinterpret_cast<const unsigned char *>(data.CStr()), data.GetLength());
		String msg = reinterpret_cast<char *>(internal_err_str);
		yajl_free_error(handle, internal_err_str);

		yajl_free(handle);

		/* throw saved exception (if there is one) */
		context.ThrowException();

		BOOST_THROW_EXCEPTION(std::invalid_argument(msg));
	}

	yajl_free(handle);

	return context.GetValue();
}
