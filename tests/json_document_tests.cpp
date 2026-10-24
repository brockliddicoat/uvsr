#include "json_document.h"
#include "json_output.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace uvsr::json;

namespace
{
    void Check(bool value, const char* message)
    {
        if (!value) { fprintf(stderr,"JSON contract failed: %s\n",message); exit(1); }
    }
    struct Text
    {
        char* data;
        size_t size;
        explicit Text(size_t count) : data(static_cast<char*>(malloc(count + 1))),size(count)
        {
            Check(data != nullptr,"fixture allocation failed");
            data[count] = '\0';
        }
        ~Text() { free(data); }
        Text(const Text&) = delete;
        Text& operator=(const Text&) = delete;
        Text(Text&& other) noexcept : data(other.data),size(other.size) { other.data = nullptr; other.size = 0; }
    };
    Text Serialized(Value value)
    {
        Error error;
        size_t size = 0;
        Check(MeasureSerialized(value,size,error),"cannot measure JSON output");
        Text text(size);
        Check(WriteSerialized(value,text.data,size + 1,size,error) && size == text.size,"cannot serialize JSON output");
        return text;
    }
    void Expect(Value value, const char* expected)
    {
        const Text text = Serialized(value);
        Check(text.size == strlen(expected) && memcmp(text.data,expected,text.size) == 0,"serialized bytes differ");
    }
    Document Parsed(TextView value, unsigned depth = 64)
    {
        Document document;
        Error error;
        if (!document.Parse(value,error,depth))
        {
            fprintf(stderr,"fixture parse error at byte %zu: %s\n",error.byte,error.message);
            exit(1);
        }
        return document;
    }

    void Grammar()
    {
        struct Pair { const char* input; const char* output; };
        const Pair valid[] = {
            {"null","null"},{" true ","true"},{"false","false"},{"-0","-0"},
            {"1.25","1.25"},{"-2e3","-2e3"},{"1E+20","1E+20"},
            {"9999999999999999999999999999999999","9999999999999999999999999999999999"},
            {"\"\\ud83d\\ude80\"","\"\xf0\x9f\x9a\x80\""},
            {"\"\\u0000\\u001f\\b\\f\\n\\r\\t\\/\"","\"\\u0000\\u001f\\b\\f\\n\\r\\t/\""},
            {" {\"b\": [null,true,1e+2], \"a\":\"\\u0061\"} ","{\"b\":[null,true,1e+2],\"a\":\"a\"}"},
            {"{\"\": [],\"a\\u0000b\":{},\"a\":[[false]]}","{\"\":[],\"a\\u0000b\":{},\"a\":[[false]]}"}
        };
        for (const Pair& pair : valid)
        {
            const Document document = Parsed(pair.input);
            Expect(document.Root(),pair.output);
            const Document again = Parsed(pair.output);
            Check(Equal(document.Root(),again.Root()),"round trip changed a value");
        }
        const char* invalid[] = {
            "", " ", "-", "-01", "+1", "1.", "1e", "1e+", "1e-", "1e999", "1e-999", "01", "1 2",
            "[1,]", "[,1]", "[1", "{", "{\"a\"}", "{\"a\":}", "{\"a\":1,}",
            "{\"a\":1,\"\\u0061\":2}", "[\"a\":]", "nul", "True", "//comment\n1", "\"\\ud800\"", "\"\\udc00\"",
            "\"\\ud800\\u0000\"", "\"\\u001\"", "\"\\x20\"", "\"\n\"", "\"unterminated", "\"\\",
            "\"\x80\"", "\"\xc0\xaf\"", "\"\xe0\x80\x80\"", "\"\xed\xa0\x80\"",
            "\"\xf4\x90\x80\x80\"", "\"\xe2\x82\"", "\xef\xbb\xbf{}"
        };
        Document previous = Parsed("{\"keep\":\"previous bytes\"}");
        const TextView oldText = previous.Root().Find("keep").Text();
        for (const char* input : invalid)
        {
            Error error;
            Check(!previous.Parse(input,error) && error.code == ErrorCode::InvalidInput,"invalid JSON was accepted or misclassified");
            Check(previous.Root().Find("keep").Text().data == oldText.data,"failed parse invalidated a borrowed view");
            Expect(previous.Root(),"{\"keep\":\"previous bytes\"}");
        }
        struct Failure { const char* input; size_t byte; const char* message; };
        const Failure offsets[] = {
            {"",0,"unexpected end of input"},{"-",1,"number digits expected"},
            {"01",1,"leading zero"},{"1.",2,"fraction digits expected"},{"1e+",3,"exponent digits expected"},
            {"{}x",2,"trailing data"},{"[1,]",3,"value expected"},{"{\"a\" 1}",5,"colon expected"},
            {"\"\\ud800\"",7,"high surrogate lacks low surrogate"},{"\"\\x20\"",3,"invalid escape"}
        };
        for (const Failure& failure : offsets)
        {
            Error error;
            Check(!previous.Parse(failure.input,error),"bad offset fixture passed");
            Check(error.byte == failure.byte && strcmp(error.message,failure.message) == 0,"JSON error position or message changed");
        }
        const Document nullKey = Parsed("{\"a\\u0000b\":1,\"a\":2}");
        int64_t value = 0;
        Check(Integer(nullKey.Root().Find({"a\0b",3}),value) && value == 1,"embedded NUL property lookup changed");
        Check(Integer(nullKey.Root().Find("a"),value) && value == 2,"property names were truncated at NUL");
        printf("general JSON cases: %zu valid, %zu invalid, %zu exact diagnostics\n",
            sizeof(valid)/sizeof(*valid),sizeof(invalid)/sizeof(*invalid),sizeof(offsets)/sizeof(*offsets));
    }

    void SignedAndDepth()
    {
        const int64_t numbers[] = {INT64_MIN,INT64_C(-9007199254740993),0,INT64_C(9007199254740993),INT64_MAX};
        for (int64_t expected : numbers)
        {
            Error error;
            Document built;
            Check(built.Assign(Seed::Integer(expected),error),"cannot construct integer");
            const Text token = Serialized(built.Root());
            Document parsed = Parsed({token.data,token.size});
            int64_t actual = 7;
            Check(ValidateSigned(parsed.Root(),error) && Integer(parsed.Root(),actual) && actual == expected,
                "signed contract integer lost precision");
        }
        const char* invalid[] = {"null","[null]","{\"x\":null}","1.0","1e0","9223372036854775808","-9223372036854775809"};
        for (const char* input : invalid)
        {
            const Document parsed = Parsed(input);
            Error error;
            Check(!ValidateSigned(parsed.Root(),error),"signed contract accepted null or a non-int64 token");
        }
        constexpr unsigned depths[] = {0,16,32,64};
        for (unsigned depth : depths)
        {
            Text nested(size_t(depth) * 2 + 1);
            memset(nested.data,'[',depth); nested.data[depth] = '0'; memset(nested.data + depth + 1,']',depth);
            Document document = Parsed({nested.data,nested.size},depth);
            if (depth)
            {
                Error error;
                Check(!document.Parse({nested.data,nested.size},error,depth - 1) &&
                    error.byte == depth && strcmp(error.message,"nesting limit exceeded") == 0,"depth limit changed");
            }
        }
        constexpr unsigned depth = 12000;
        Text nested(size_t(depth) * 2 + 1);
        memset(nested.data,'[',depth); nested.data[depth] = '7'; memset(nested.data + depth + 1,']',depth);
        Document deep = Parsed({nested.data,nested.size},depth);
        Error error;
        Document copy;
        Check(copy.Assign(deep.Root(),error),"deep clone failed");
        const Text text = Serialized(copy.Root());
        Check(text.size == nested.size && memcmp(text.data,nested.data,text.size) == 0 && Equal(deep.Root(),copy.Root()),
            "deep clone, equality or serialization failed");
        Check(ValidateSigned(deep.Root(),error),"deep signed traversal failed");
        printf("deep JSON: %u containers, %zu nodes, %zu retained bytes\n",depth,deep.NodeCount(),deep.StorageBytes());
        deep.Clear(); copy.Clear();
    }

    void BuildersAndEquality()
    {
        const Document source = Parsed("{\"nested\":{\"y\":[true,null],\"x\":\"copied\"},\"n\":1.0}");
        Document record;
        Error error;
        const Member members[] = {{"version",Seed::Integer(7)},{"data",source.Root().Find("nested")},
            {"name",Seed::String("first")},{"empty",Seed::Array()}};
        Check(record.MakeObject(members,sizeof(members)/sizeof(*members),error),"object construction failed");
        Expect(record.Root(),"{\"version\":7,\"data\":{\"y\":[true,null],\"x\":\"copied\"},\"name\":\"first\",\"empty\":[]}");
        Check(record.Replace("name",Seed::String("second"),error),"record replacement failed");
        Check(record.Replace("data",record.Root().Find("name"),error),"self-borrowed replacement failed");
        Expect(record.Root(),"{\"version\":7,\"data\":\"second\",\"name\":\"second\",\"empty\":[]}");
        const Text previous = Serialized(record.Root());
        Check(!record.Replace("missing",{},error) && error.code == ErrorCode::InvalidOperation,"missing property replacement passed");
        Expect(record.Root(),previous.data);
        const Member duplicate[] = {{"a",Seed::Integer(1)},{"a",Seed::Integer(2)}};
        Check(!record.MakeObject(duplicate,2,error),"duplicate builder property passed");
        Expect(record.Root(),previous.data);
        Document array;
        Check(array.Assign(Seed::Array(),error) && array.Append(source.Root().Find("nested"),error) &&
            array.Append(Seed::Boolean(false),error) && array.Append(array.Root().First(),error),"array append failed");
        Expect(array.Root(),"[{\"y\":[true,null],\"x\":\"copied\"},false,{\"y\":[true,null],\"x\":\"copied\"}]");
        const Document reordered = Parsed("{\"n\":1.0,\"nested\":{\"x\":\"copied\",\"y\":[true,null]}}");
        Check(Equal(source.Root(),reordered.Root()),"object order affected semantic equality");
        const Document zero = Parsed("{\"n\":[0,9007199254740993]}");
        const Document negativeZero = Parsed("{\"n\":[-0,9007199254740993]}");
        const Document nearby = Parsed("{\"n\":[0,9007199254740992]}");
        Check(!Equal(zero.Root(),negativeZero.Root()) &&
            Equal(zero.Root(),negativeZero.Root(),NumberComparison::Integer) &&
            !Equal(zero.Root(),nearby.Root(),NumberComparison::Integer),"signed integer comparison lost token or exact int64 semantics");
        const char* differences[] = {"{\"nested\":{\"y\":[null,true],\"x\":\"copied\"},\"n\":1.0}",
            "{\"nested\":{\"y\":[true,null],\"x\":\"copied\"},\"n\":1}","{}","null"};
        for (const char* different : differences)
        {
            const Document other = Parsed(different);
            Check(!Equal(source.Root(),other.Root()),"different arrays, number tokens or kinds compared equal");
        }
        Check(!Value{}.IsValid() && !Equal(Value{},source.Root()) && !source.Root().Find("missing").IsValid(),
            "missing JSON view was valid");
        Document moved(static_cast<Document&&>(record));
        Expect(moved.Root(),previous.data);
        Expect(record.Root(),"null");
        Check(moved.Assign(moved.Root().Find("name"),error),"self-subtree assignment failed");
        Expect(moved.Root(),"\"second\"");
        Document inventory;
        Check(inventory.Assign(Seed::Array(),error),"cannot create inventory array");
        constexpr unsigned entries = 16384;
        for (unsigned index = 0; index < entries; ++index)
            Check(inventory.Append(Seed::Integer(index),error),"inventory growth failed");
        unsigned index = 0;
        for (Value item = inventory.Root().First(); item.IsValid(); item = item.Next(),++index)
        {
            int64_t number = -1;
            Check(Integer(item,number) && number == index,"inventory append order changed");
        }
        Check(index == entries && inventory.Root().Count() == entries,"inventory entries were lost");
        printf("wide JSON: %u appended entries, %zu retained bytes\n",entries,inventory.StorageBytes());
    }

    void Exhaustion()
    {
        const Document source = Parsed("{\"child\":[1,2,3],\"text\":\"source\"}");
        const Member members[] = {{"copy",source.Root()},{"value",Seed::Integer(9)}};
        unsigned failures = 0;
        for (unsigned operation = 0; operation < 5; ++operation)
        {
            bool finished = false;
            for (size_t after = 0; after < 8; ++after)
            {
                Document document = Parsed(operation == 4 ? "[\"keep\"]" : "{\"keep\":\"old\"}");
                const Text previous = Serialized(document.Root());
                const Value view = operation == 4 ? document.Root().First() : document.Root().Find("keep");
                const char* oldText = view.Text().data;
                Error error;
                FailAllocationAfter(after);
                bool success = false;
                switch (operation)
                {
                case 0: success = document.Parse("{\"new\":[true,\"text\"]}",error); break;
                case 1: success = document.Assign(source.Root(),error); break;
                case 2: success = document.MakeObject(members,2,error); break;
                case 3: success = document.Replace("keep",source.Root(),error); break;
                case 4: success = document.Append(source.Root(),error); break;
                }
                ClearAllocationFailure();
                if (success) { finished = true; break; }
                ++failures;
                Check(error.code == ErrorCode::OutOfMemory,"allocation failure lost its explicit category");
                Check(view.Text().data == oldText,"failed allocation invalidated an existing borrow");
                Expect(document.Root(),previous.data);
            }
            Check(finished,"allocation failure retry did not succeed");
        }
        Error error;
        Document retained = Parsed("7");
        Check(!retained.Parse({nullptr,1},error) && error.code == ErrorCode::InvalidInput,"null input range passed");
        Check(!retained.MakeObject(nullptr,1,error),"null member range passed");
        Check(!retained.MakeObject(members,SIZE_MAX,error),"oversized member range passed");
        Check(!retained.Assign(Value{},error) && error.code == ErrorCode::InvalidView,"invalid copy passed");
        Check(!retained.Assign(Seed::String({"\xff",1}),error) && error.code == ErrorCode::InvalidInput,"invalid UTF-8 seed passed");
        Seed invalidSeed; invalidSeed.kind = static_cast<SeedKind>(255);
        Check(!retained.Assign(invalidSeed,error) && error.code == ErrorCode::InvalidInput,"invalid seed kind passed");
        Expect(retained.Root(),"7");
        char buffer[32]; memset(buffer,'x',sizeof(buffer));
        size_t size = 123;
        Check(!WriteSerialized(source.Root(),buffer,1,size,error) && size == 123 && buffer[0] == 'x',
            "small output capacity published partial data");
        Check(!WriteEscaped("\n",buffer,2,size,error) && size == 123 && buffer[0] == 'x',"small escape output published partial data");
        char overlap[32] = "same input";
        Check(!WriteEscaped({overlap,10},overlap,sizeof(overlap),size,error) &&
            strcmp(overlap,"same input") == 0 && size == 123,"overlapping output changed the input");
        FailAllocationAfter(0);
        Check(Equal(source.Root(),source.Root()) && ValidateSigned(source.Root(),error),"read-only traversal allocated");
        Check(!retained.Assign(source.Root(),error) && error.code == ErrorCode::OutOfMemory,"read-only traversal consumed an allocation");
        ClearAllocationFailure();
        printf("JSON allocation failures and unchanged-document retries: %u\n",failures);
    }
}

namespace
{
    bool EmitOutput(OutputWriter& output, const void*) noexcept
    {
        return output.Raw("{\"text\":") && output.String({"x\n\"\\\0", 5}) &&
            output.Raw(",\"signed\":") && output.Integer(INT64_MIN) &&
            output.Raw(",\"unsigned\":") && output.Unsigned(UINT64_MAX) &&
            output.Raw(",\"enabled\":") && output.Boolean(true) && output.Raw("}\n");
    }

    void CheckedStringParts()
    {
        const TextView parts[]{"x\n", {}, {"\"\\\0", 3}, {nullptr, 0}};
        const auto emit = [](OutputWriter& writer, const void* context) noexcept
        { return writer.StringParts(static_cast<const TextView*>(context), 4); };
        EncodedText joined(emit, parts);
        EncodedText single([](OutputWriter& writer, const void*) noexcept
        { return writer.String({"x\n\"\\\0", 5}); });
        Check(joined.IsValid() && single.IsValid() && SameText(joined.View(), single.View()),
            "fragment boundaries changed string escaping");
        EncodedText empty([](OutputWriter& writer, const void*) noexcept
        { return writer.StringParts(nullptr, 0); });
        Check(empty.IsValid() && SameText(empty.View(), "\"\""), "empty string parts failed");
        for (unsigned mode = 0; mode < 3; ++mode)
        {
            EncodedText invalid([](OutputWriter& writer, const void* context) noexcept
            {
                const auto selected = *static_cast<const unsigned*>(context);
                const TextView bad[]{"valid", {nullptr, 1}, "unreached"};
                if (!writer.Raw("[")) return false;
                const bool accepted = selected == 0 ? writer.StringParts(nullptr, 1) :
                    writer.StringParts(bad, selected == 1 ? 3 : SIZE_MAX);
                return !accepted && writer.Size() == 1 && !writer.Raw("]");
            }, &mode);
            Check(!invalid.IsValid() && invalid.Size() == 0 &&
                invalid.Failure().code == ErrorCode::InvalidInput && invalid.Failure().byte == 0,
                "invalid string parts published data or lost the first error");
        }
        unsigned pass = 0;
        EncodedText growing([](OutputWriter& writer, const void* context) noexcept
        {
            auto& count = *static_cast<unsigned*>(const_cast<void*>(context));
            const TextView values[]{"x", count++ ? "longer" : ""};
            return writer.StringParts(values, 2);
        }, &pass);
        Check(!growing.IsValid() && growing.Size() == 0 && growing.Failure().code == ErrorCode::Capacity,
            "growing string parts published a partial write");
        FailAllocationAfter(0);
        EncodedText failed(emit, parts);
        ClearAllocationFailure();
        Check(!failed.IsValid() && failed.Size() == 0 && failed.Failure().code == ErrorCode::OutOfMemory,
            "fragmented string allocation failure published data");
        FailAllocationAfter(1);
        EncodedText retry(emit, parts);
        EncodedText exhausted(emit, parts);
        ClearAllocationFailure();
        Check(retry.IsValid() && SameText(retry.View(), joined.View()) && !exhausted.IsValid() &&
            exhausted.Failure().code == ErrorCode::OutOfMemory, "string fragments did not use one allocation");
    }

    void CheckedOutput()
    {
        CheckedStringParts();
        EncodedText output(EmitOutput);
        constexpr char expected[] = "{\"text\":\"x\\n\\\"\\\\\\u0000\",\"signed\":-9223372036854775808,\"unsigned\":18446744073709551615,\"enabled\":true}\n";
        Check(output.IsValid() && output.Size() == sizeof(expected) - 1 &&
            memcmp(output.Data(), expected, sizeof(expected)) == 0, "checked output changed bytes or its terminator");
        const char* borrowed = output.Data();
        EncodedText moved(static_cast<EncodedText&&>(output));
        Check(moved.Data() == borrowed && !output.IsValid(), "output movement copied or retained the old owner");
        output = static_cast<EncodedText&&>(moved);
        Check(output.Data() == borrowed && !moved.IsValid(), "output move assignment lost ownership");

        FailAllocationAfter(0);
        EncodedText failed(EmitOutput);
        Check(!failed.IsValid() && failed.Size() == 0 && failed.Failure().code == ErrorCode::OutOfMemory &&
            memcmp(output.Data(), expected, sizeof(expected)) == 0, "output allocation failure published data or changed the previous owner");
        ClearAllocationFailure();
        EncodedText retry(EmitOutput);
        Check(retry.IsValid() && SameText(retry.View(), output.View()), "output retry changed bytes");

        EncodedText missing(nullptr);
        Check(!missing.IsValid() && missing.Failure().code == ErrorCode::InvalidInput, "missing emitter passed");
        EncodedText ignoredFailure([](OutputWriter& writer, const void*) noexcept
        {
            const bool invalid = writer.String({nullptr, 1});
            const bool later = writer.Raw("null");
            return !invalid && !later;
        });
        Check(!ignoredFailure.IsValid() && ignoredFailure.Failure().code == ErrorCode::InvalidInput,
            "emitter return value hid a failed write");
        unsigned pass = 0;
        EncodedText growing([](OutputWriter& writer, const void* context) noexcept
        {
            auto& count = *static_cast<unsigned*>(const_cast<void*>(context));
            return writer.Raw(count++ ? "null" : "[]");
        }, &pass);
        Check(!growing.IsValid() && growing.Size() == 0 && growing.Failure().code == ErrorCode::Capacity,
            "growing output published a partial write");
        pass = 0;
        EncodedText shrinking([](OutputWriter& writer, const void* context) noexcept
        {
            auto& count = *static_cast<unsigned*>(const_cast<void*>(context));
            return writer.Raw(count++ ? "[]" : "null");
        }, &pass);
        Check(!shrinking.IsValid() && shrinking.Size() == 0 && shrinking.Failure().code == ErrorCode::InvalidOperation,
            "changed output size was silently published");
        FailAllocationAfter(0);
        EncodedText rejected([](OutputWriter& writer, const void*) noexcept
        { return writer.Reject(ErrorCode::InvalidInput, "fixture rejected input"); });
        EncodedText allocationStillPending(EmitOutput);
        Check(!rejected.IsValid() && rejected.Failure().code == ErrorCode::InvalidInput &&
            !allocationStillPending.IsValid() && allocationStillPending.Failure().code == ErrorCode::OutOfMemory,
            "failed measurement consumed backing storage");
        ClearAllocationFailure();
    }
}

int main()
{
    Grammar();
    SignedAndDepth();
    BuildersAndEquality();
    Exhaustion();
    CheckedOutput();
    return 0;
}
