#include "json_document.h"
#include "json_output.h"

#include <charconv>
#include <math.h>
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace uvsr::json
{
    namespace
    {
        constexpr uint32_t None = UINT32_MAX;
        constexpr char Empty[] = "";
        struct Slice { size_t offset; size_t size; };

        bool Fail(Error& error, ErrorCode code, const char* message, size_t byte = 0) noexcept
        {
            error = {code, byte, message};
            return false;
        }

#if defined(UVSR_JSON_TEST_HOOKS)
        thread_local size_t allocationsUntilFailure = SIZE_MAX;
#endif
        bool AllocationFails() noexcept
        {
#if defined(UVSR_JSON_TEST_HOOKS)
            if (allocationsUntilFailure == 0)
            {
                allocationsUntilFailure = SIZE_MAX;
                return true;
            }
            if (allocationsUntilFailure != SIZE_MAX) --allocationsUntilFailure;
#endif
            return false;
        }

        bool AddSize(size_t& total, size_t count, Error& error) noexcept
        {
            if (count > size_t(PTRDIFF_MAX) - total)
                return Fail(error, ErrorCode::Capacity, "JSON storage capacity exceeded");
            total += count;
            return true;
        }
        bool White(char value) noexcept
        {
            return value == ' ' || value == '\t' || value == '\r' || value == '\n';
        }
        bool Digit(char value) noexcept { return value >= '0' && value <= '9'; }
        bool ReadUtf8(TextView input, size_t& position, unsigned char first, Error& error) noexcept
        {
            unsigned count = 0;
            uint32_t codePoint = 0;
            if (first >= 0xc2 && first <= 0xdf) { count = 1; codePoint = first & 0x1f; }
            else if (first >= 0xe0 && first <= 0xef) { count = 2; codePoint = first & 0x0f; }
            else if (first >= 0xf0 && first <= 0xf4) { count = 3; codePoint = first & 0x07; }
            else return Fail(error,ErrorCode::InvalidInput,"invalid UTF-8 lead byte",position);
            for (unsigned index = 0; index < count; ++index)
            {
                if (position == input.size) return Fail(error,ErrorCode::InvalidInput,"truncated UTF-8 sequence",position);
                const unsigned char next = static_cast<unsigned char>(input.data[position++]);
                if ((next & 0xc0u) != 0x80u) return Fail(error,ErrorCode::InvalidInput,"invalid UTF-8 continuation byte",position);
                codePoint = (codePoint << 6) | (next & 0x3fu);
            }
            const uint32_t minimum = count == 1 ? 0x80u : count == 2 ? 0x800u : 0x10000u;
            return (codePoint >= minimum && codePoint <= 0x10ffffu &&
                !(codePoint >= 0xd800u && codePoint <= 0xdfffu)) ||
                Fail(error,ErrorCode::InvalidInput,"invalid UTF-8 code point",position);
        }
        bool ValidText(TextView input, Error& error) noexcept
        {
            if (!input.IsValid()) return Fail(error,ErrorCode::InvalidInput,"invalid JSON text view");
            for (size_t position = 0; position < input.size;)
            {
                const unsigned char first = static_cast<unsigned char>(input.data[position++]);
                if (first >= 0x80 && !ReadUtf8(input,position,first,error)) return false;
            }
            return true;
        }
        bool Delimiter(char value) noexcept
        {
            return White(value) || value == ',' || value == ':' || value == '[' ||
                value == ']' || value == '{' || value == '}' || value == '"';
        }
        bool SameValue(Value left, Value right) noexcept
        {
            return left.owner == right.owner && left.index == right.index;
        }
        bool Overlaps(TextView input, const char* output, size_t capacity) noexcept
        {
            if (!input.size || !capacity) return false;
            const uintptr_t source = reinterpret_cast<uintptr_t>(input.data);
            const uintptr_t destination = reinterpret_cast<uintptr_t>(output);
            return source <= destination ? destination - source < input.size : source - destination < capacity;
        }
        size_t Growth(size_t capacity, size_t required, size_t maximum) noexcept
        {
            if (required > maximum) return 0;
            if (!capacity) capacity = maximum < 16 ? maximum : 16;
            while (capacity < required) capacity = capacity > maximum / 2 ? maximum : capacity * 2;
            return capacity;
        }
        Value AfterSubtree(Value value, Value root) noexcept
        {
            while (!SameValue(value, root))
            {
                if (Value next = value.Next(); next.IsValid()) return next;
                value = value.Parent();
                if (!value.IsValid()) return {};
            }
            return {};
        }
        Value NextValue(Value value, Value root) noexcept
        {
            if (Value child = value.First(); child.IsValid()) return child;
            return AfterSubtree(value, root);
        }

        struct Counts
        {
            size_t nodes = 0;
            size_t text = 0;
            size_t containers = 0;
        };

        bool CountInput(TextView input, Counts& count, Error& error) noexcept
        {
            // count quoted keys too. this bounds every parsed prefix, including
            // malformed input whose quotes cannot yet be classified as keys.
            for (size_t index = 0; index < input.size;)
            {
                const char value = input.data[index++];
                if (White(value) || value == ',' || value == ':' || value == ']' || value == '}') continue;
                if (!AddSize(count.nodes, 1, error)) return false;
                if (value == '[' || value == '{') { ++count.containers; continue; }
                if (value == '"')
                {
                    const size_t begin = index;
                    while (index < input.size)
                    {
                        const char current = input.data[index++];
                        if (current == '"') break;
                        if (current == '\\' && index < input.size) ++index;
                    }
                    if (!AddSize(count.text, index - begin + 1, error)) return false;
                }
                else
                {
                    const size_t begin = index - 1;
                    while (index < input.size && !Delimiter(input.data[index])) ++index;
                    if ((value == '-' || Digit(value)) && !AddSize(count.text, index - begin + 1, error))
                        return false;
                }
            }
            if (!count.nodes) count.nodes = 1;
            return true;
        }
    }

    struct Document::Node
    {
        Slice name;
        Slice text;
        double number;
        uint32_t parent;
        uint32_t first;
        uint32_t last;
        uint32_t next;
        uint32_t count;
        Kind kind;
        bool boolean;
    };

    TextView::TextView(const char* terminated) noexcept
        : data(terminated ? terminated : Empty), size(terminated ? strlen(terminated) : 0) {}

    bool TextView::IsValid() const noexcept
    {
        return size <= size_t(PTRDIFF_MAX) && (data || !size);
    }
    bool SameText(TextView left, TextView right) noexcept
    {
        return left.IsValid() && right.IsValid() && left.size == right.size &&
            (!left.size || memcmp(left.data, right.data, left.size) == 0);
    }
    const Document::Node* Document::Get(uint32_t index) const noexcept
    {
        static constexpr Node nullNode{{0,0},{0,0},0.0,None,None,None,None,0,Kind::Null,false};
        if (!m_Count && index == 0) return &nullNode;
        return index < m_Count ? m_Nodes + index : nullptr;
    }
    bool Value::IsValid() const noexcept { return owner && owner->Get(index); }
    Kind Value::Type() const noexcept
    {
        const auto* node = owner ? owner->Get(index) : nullptr;
        return node ? node->kind : Kind::Null;
    }
    bool Value::Boolean() const noexcept
    {
        const auto* node = owner ? owner->Get(index) : nullptr;
        return node && node->boolean;
    }
    double Value::Number() const noexcept
    {
        const auto* node = owner ? owner->Get(index) : nullptr;
        return node ? node->number : 0.0;
    }
    TextView Value::Text() const noexcept
    {
        const auto* node = owner ? owner->Get(index) : nullptr;
        return node && node->text.size ? TextView{owner->m_Text + node->text.offset, node->text.size} : TextView{Empty,0};
    }
    TextView Value::Name() const noexcept
    {
        const auto* node = owner ? owner->Get(index) : nullptr;
        return node && node->name.size ? TextView{owner->m_Text + node->name.offset, node->name.size} : TextView{Empty,0};
    }
    size_t Value::Count() const noexcept
    {
        const auto* node = owner ? owner->Get(index) : nullptr;
        return node ? node->count : 0;
    }
    Value Value::First() const noexcept
    {
        const auto* node = owner ? owner->Get(index) : nullptr;
        return node && node->first != None ? Value{owner,node->first} : Value{};
    }
    Value Value::Next() const noexcept
    {
        const auto* node = owner ? owner->Get(index) : nullptr;
        return node && node->next != None ? Value{owner,node->next} : Value{};
    }
    Value Value::Parent() const noexcept
    {
        const auto* node = owner ? owner->Get(index) : nullptr;
        return node && node->parent != None ? Value{owner,node->parent} : Value{};
    }
    Value Value::Find(TextView name) const noexcept
    {
        if (Type() != Kind::Object || !name.IsValid()) return {};
        for (Value member = First(); member.IsValid(); member = member.Next())
            if (SameText(member.Name(), name)) return member;
        return {};
    }
    Value Value::At(size_t position) const noexcept
    {
        if (Type() != Kind::Array || position >= Count()) return {};
        Value value = First();
        while (position--) value = value.Next();
        return value;
    }

    Document::~Document() noexcept { Clear(); }
    Document::Document(Document&& other) noexcept { Adopt(other); }
    Document& Document::operator=(Document&& other) noexcept
    {
        if (this != &other) Adopt(other);
        return *this;
    }
    void Document::Clear() noexcept
    {
        delete[] m_Nodes;
        free(m_Text);
        m_Nodes = nullptr; m_Text = nullptr;
        m_Count = m_Capacity = 0;
        m_TextSize = m_TextCapacity = 0;
    }
    void Document::Adopt(Document& candidate) noexcept
    {
        Clear();
        m_Nodes = candidate.m_Nodes; m_Text = candidate.m_Text;
        m_Count = candidate.m_Count; m_Capacity = candidate.m_Capacity;
        m_TextSize = candidate.m_TextSize; m_TextCapacity = candidate.m_TextCapacity;
        candidate.m_Nodes = nullptr; candidate.m_Text = nullptr;
        candidate.m_Count = candidate.m_Capacity = 0;
        candidate.m_TextSize = candidate.m_TextCapacity = 0;
    }
    size_t Document::StorageBytes() const noexcept
    {
        return size_t(m_Capacity) * sizeof(Node) + m_TextCapacity;
    }
    bool Document::Prepare(size_t nodes, size_t textBytes, Error& error) noexcept
    {
        if (!nodes || nodes > UINT32_MAX || nodes > size_t(PTRDIFF_MAX) / sizeof(Node) ||
            textBytes > size_t(PTRDIFF_MAX) - nodes * sizeof(Node))
            return Fail(error, ErrorCode::Capacity, "JSON storage capacity exceeded");
        Node* storage = AllocationFails() ? nullptr : new (std::nothrow) Node[nodes];
        if (!storage) return Fail(error, ErrorCode::OutOfMemory, "out of memory in JSON nodes");
        char* text = textBytes ? (AllocationFails() ? nullptr : static_cast<char*>(malloc(textBytes))) : nullptr;
        if (textBytes && !text)
        {
            delete[] storage;
            return Fail(error, ErrorCode::OutOfMemory, "out of memory in JSON text");
        }
        m_Nodes = storage; m_Capacity = uint32_t(nodes);
        m_Text = text; m_TextCapacity = textBytes;
        return true;
    }
    bool Document::AddText(TextView value, size_t& offset, Error& error) noexcept
    {
        offset = 0;
        if (!value.IsValid()) return Fail(error, ErrorCode::InvalidInput, "invalid JSON text view");
        if (!value.size) return true;
        if (value.size >= m_TextCapacity - m_TextSize)
            return Fail(error, ErrorCode::Capacity, "JSON text capacity exceeded");
        offset = m_TextSize;
        memcpy(m_Text + m_TextSize, value.data, value.size);
        m_TextSize += value.size;
        m_Text[m_TextSize++] = '\0';
        return true;
    }
    bool Document::AddNode(Kind kind, uint32_t parent, uint32_t& index, Error& error) noexcept
    {
        if (m_Count >= m_Capacity)
            return Fail(error, ErrorCode::Capacity, "JSON node capacity exceeded");
        index = m_Count++;
        m_Nodes[index] = {{0,0},{0,0},0.0,parent,None,None,None,0,kind,false};
        if (parent != None)
        {
            Node& owner = m_Nodes[parent];
            if (owner.last == None) owner.first = index;
            else m_Nodes[owner.last].next = index;
            owner.last = index;
            ++owner.count;
        }
        return true;
    }

    struct ParserState
    {
        enum class Phase : uint8_t { First, Next, Comma };
        struct Frame { uint32_t node; Phase phase; };
        Document& document;
        TextView input;
        Error& error;
        unsigned maximumDepth;
        size_t position = 0;
        Frame* frames = nullptr;
        size_t frameCount = 0;
        size_t frameCapacity = 0;
        ~ParserState() noexcept { delete[] frames; }

        bool Invalid(const char* reason) noexcept { return Fail(error, ErrorCode::InvalidInput, reason, position); }
        bool Emit(char value) noexcept
        {
            if (document.m_TextSize == document.m_TextCapacity)
                return Fail(error, ErrorCode::Capacity, "JSON text capacity exceeded", position);
            document.m_Text[document.m_TextSize++] = value;
            return true;
        }
        void Skip() noexcept { while (position < input.size && White(input.data[position])) ++position; }
        bool Consume(char value) noexcept
        {
            if (position == input.size || input.data[position] != value) return false;
            ++position;
            return true;
        }
        bool HexWord(uint32_t& result) noexcept
        {
            if (input.size - position < 4) return Invalid("truncated Unicode escape");
            result = 0;
            for (unsigned index = 0; index < 4; ++index)
            {
                const char value = input.data[position++];
                result <<= 4;
                if (value >= '0' && value <= '9') result += value - '0';
                else if (value >= 'a' && value <= 'f') result += value - 'a' + 10;
                else if (value >= 'A' && value <= 'F') result += value - 'A' + 10;
                else return Invalid("invalid Unicode escape");
            }
            return true;
        }
        bool CodePoint(uint32_t value) noexcept
        {
            if (value <= 0x7f) return Emit(char(value));
            if (value <= 0x7ff) return Emit(char(0xc0u | (value >> 6))) && Emit(char(0x80u | (value & 0x3f)));
            if (value <= 0xffff)
                return Emit(char(0xe0u | (value >> 12))) && Emit(char(0x80u | ((value >> 6) & 0x3f))) &&
                    Emit(char(0x80u | (value & 0x3f)));
            return Emit(char(0xf0u | (value >> 18))) && Emit(char(0x80u | ((value >> 12) & 0x3f))) &&
                Emit(char(0x80u | ((value >> 6) & 0x3f))) && Emit(char(0x80u | (value & 0x3f)));
        }
        bool Escape() noexcept
        {
            if (position == input.size) return Invalid("unterminated escape");
            switch (input.data[position++])
            {
            case '"': return Emit('"');
            case '\\': return Emit('\\');
            case '/': return Emit('/');
            case 'b': return Emit('\b');
            case 'f': return Emit('\f');
            case 'n': return Emit('\n');
            case 'r': return Emit('\r');
            case 't': return Emit('\t');
            case 'u': break;
            default: return Invalid("invalid escape");
            }
            uint32_t codePoint = 0;
            if (!HexWord(codePoint)) return false;
            if (codePoint >= 0xd800 && codePoint <= 0xdbff)
            {
                if (input.size - position < 2 || input.data[position] != '\\' || input.data[position + 1] != 'u')
                    return Invalid("high surrogate lacks low surrogate");
                position += 2;
                uint32_t low = 0;
                if (!HexWord(low)) return false;
                if (low < 0xdc00 || low > 0xdfff) return Invalid("invalid low surrogate");
                codePoint = 0x10000u + ((codePoint - 0xd800u) << 10) + (low - 0xdc00u);
            }
            else if (codePoint >= 0xdc00 && codePoint <= 0xdfff) return Invalid("unexpected low surrogate");
            return CodePoint(codePoint);
        }
        bool Utf8(unsigned char first) noexcept
        {
            const size_t begin = position - 1;
            if (!ReadUtf8(input,position,first,error)) return false;
            for (size_t index = begin; index < position; ++index)
                if (!Emit(input.data[index])) return false;
            return true;
        }
        bool String(Slice& result) noexcept
        {
            ++position;
            result = {document.m_TextSize,0};
            while (position < input.size)
            {
                const unsigned char value = static_cast<unsigned char>(input.data[position++]);
                if (value == '"')
                {
                    result.size = document.m_TextSize - result.offset;
                    return Emit('\0');
                }
                if (value < 0x20) return Invalid("unescaped control character");
                if (value == '\\') { if (!Escape()) return false; }
                else if (value < 0x80) { if (!Emit(char(value))) return false; }
                else if (!Utf8(value)) return false;
            }
            return Invalid("unterminated string");
        }
        bool Number(uint32_t index) noexcept
        {
            const size_t begin = position;
            Consume('-');
            if (position == input.size) return Invalid("number digits expected");
            if (input.data[position] == '0')
            {
                ++position;
                if (position < input.size && Digit(input.data[position])) return Invalid("leading zero");
            }
            else
            {
                if (input.data[position] < '1' || input.data[position] > '9') return Invalid("number digits expected");
                while (position < input.size && Digit(input.data[position])) ++position;
            }
            if (Consume('.'))
            {
                const size_t fraction = position;
                while (position < input.size && Digit(input.data[position])) ++position;
                if (position == fraction) return Invalid("fraction digits expected");
            }
            if (position < input.size && (input.data[position] == 'e' || input.data[position] == 'E'))
            {
                ++position;
                if (position < input.size && (input.data[position] == '+' || input.data[position] == '-')) ++position;
                const size_t exponent = position;
                while (position < input.size && Digit(input.data[position])) ++position;
                if (position == exponent) return Invalid("exponent digits expected");
            }
            auto& node = document.m_Nodes[index];
            const TextView token{input.data + begin, position - begin};
            node.text.size = token.size;
            if (!document.AddText(token,node.text.offset,error)) return false;
            const auto parsed = std::from_chars(token.data,token.data + token.size,node.number,std::chars_format::general);
            return (parsed.ec == std::errc{} && parsed.ptr == token.data + token.size && isfinite(node.number)) ||
                Invalid("number is outside the finite double range");
        }
        bool ParseValue(uint32_t parent, Slice name) noexcept
        {
            if (frameCount > maximumDepth) return Invalid("nesting limit exceeded");
            if (position == input.size) return Invalid("unexpected end of input");
            const char lead = input.data[position];
            Kind kind = Kind::Null;
            const char* literal = nullptr;
            switch (lead)
            {
            case '{': kind = Kind::Object; break;
            case '[': kind = Kind::Array; break;
            case '"': kind = Kind::String; break;
            case 't': kind = Kind::Boolean; literal = "true"; break;
            case 'f': kind = Kind::Boolean; literal = "false"; break;
            case 'n': literal = "null"; break;
            default:
                if (lead != '-' && !Digit(lead)) return Invalid("value expected");
                kind = Kind::Number;
            }
            uint32_t index = 0;
            if (!document.AddNode(kind,parent,index,error)) return false;
            document.m_Nodes[index].name = name;
            if (literal)
            {
                const size_t size = strlen(literal);
                if (size > input.size - position || memcmp(input.data + position,literal,size)) return Invalid("invalid literal");
                position += size;
                document.m_Nodes[index].boolean = lead == 't';
                return true;
            }
            if (kind == Kind::String) return String(document.m_Nodes[index].text);
            if (kind == Kind::Number) return Number(index);
            ++position;
            if (frameCount == frameCapacity) return Fail(error,ErrorCode::Capacity,"JSON depth storage exceeded",position);
            frames[frameCount++] = {index,Phase::First};
            return true;
        }
        bool Run(size_t containers) noexcept
        {
            if (input.size >= 3 && static_cast<unsigned char>(input.data[0]) == 0xef &&
                static_cast<unsigned char>(input.data[1]) == 0xbb && static_cast<unsigned char>(input.data[2]) == 0xbf)
                return Invalid("byte-order marks are not accepted");
            frameCapacity = containers;
            if (containers && maximumDepth < containers - 1) frameCapacity = size_t(maximumDepth) + 1;
            if (frameCapacity > size_t(PTRDIFF_MAX) / sizeof(Frame))
                return Fail(error,ErrorCode::Capacity,"JSON depth storage exceeded");
            if (frameCapacity)
            {
                frames = AllocationFails() ? nullptr : new (std::nothrow) Frame[frameCapacity];
                if (!frames) return Fail(error,ErrorCode::OutOfMemory,"out of memory in JSON parser frames");
            }
            Skip();
            if (!ParseValue(None,{0,0})) return false;
            while (frameCount)
            {
                Frame& frame = frames[frameCount - 1];
                const uint32_t parent = frame.node;
                const bool object = document.m_Nodes[parent].kind == Kind::Object;
                const char closing = object ? '}' : ']';
                Skip();
                if (frame.phase == Phase::Comma)
                {
                    if (Consume(closing)) { --frameCount; continue; }
                    if (!Consume(',')) return Invalid("comma expected");
                    frame.phase = Phase::Next;
                    Skip();
                }
                else if (frame.phase == Phase::First && Consume(closing)) { --frameCount; continue; }
                Slice name{0,0};
                if (object)
                {
                    if (position == input.size || input.data[position] != '"') return Invalid("object property name expected");
                    if (!String(name)) return false;
                    const TextView key{document.m_Text + name.offset,name.size};
                    if (Value{&document,parent}.Find(key).IsValid()) return Invalid("duplicate object property");
                    Skip();
                    if (!Consume(':')) return Invalid("colon expected");
                    Skip();
                }
                frame.phase = Phase::Comma;
                if (!ParseValue(parent,name)) return false;
            }
            Skip();
            return position == input.size || Invalid("trailing data");
        }
    };

    bool Document::Parse(TextView input, Error& error, unsigned maximumDepth) noexcept
    {
        error = {};
        if (!input.IsValid()) return Fail(error,ErrorCode::InvalidInput,"invalid JSON input view");
        Counts count;
        Document candidate;
        if (!CountInput(input,count,error) || !candidate.Prepare(count.nodes,count.text,error)) return false;
        ParserState parser{candidate,input,error,maximumDepth};
        if (!parser.Run(count.containers)) return false;
        Adopt(candidate);
        return true;
    }

    Seed::Seed(const Document& value) noexcept : Seed(value.Root()) {}
    Seed Seed::String(TextView value) noexcept { Seed result; result.kind = SeedKind::String; result.text = value; return result; }
    Seed Seed::Integer(int64_t value) noexcept { Seed result; result.kind = SeedKind::Integer; result.integer = value; return result; }
    Seed Seed::Boolean(bool value) noexcept { Seed result; result.kind = SeedKind::Boolean; result.boolean = value; return result; }
    Seed Seed::Array() noexcept { Seed result; result.kind = SeedKind::Array; return result; }
    Seed Seed::Object() noexcept { Seed result; result.kind = SeedKind::Object; return result; }

    struct BuilderState
    {
        Document& document;
        Error& error;
        size_t nodeCount = 0;
        size_t textCount = 0;

        bool CountText(TextView text) noexcept
        {
            if (!text.IsValid()) return Fail(error,ErrorCode::InvalidInput,"invalid JSON text view");
            return !text.size || AddSize(textCount,text.size + 1,error);
        }
        bool Count(Seed value, TextView name = {}) noexcept
        {
            if (value.kind > SeedKind::Object) return Fail(error,ErrorCode::InvalidInput,"invalid JSON seed kind");
            if (!ValidText(name,error) || !CountText(name)) return false;
            if (value.kind == SeedKind::Copy)
            {
                if (!value.copy.IsValid()) return Fail(error,ErrorCode::InvalidView,"invalid JSON copy view");
                for (Value item = value.copy; item.IsValid(); item = NextValue(item,value.copy))
                {
                    if (!AddSize(nodeCount,1,error) || !CountText(item.Text()) ||
                        (!SameValue(item,value.copy) && !CountText(item.Name()))) return false;
                }
                return true;
            }
            if (!AddSize(nodeCount,1,error)) return false;
            if (value.kind == SeedKind::String) return ValidText(value.text,error) && CountText(value.text);
            if (value.kind == SeedKind::Integer) return AddSize(textCount,21,error);
            return true;
        }
        bool Prepare() noexcept { return document.Prepare(nodeCount,textCount,error); }
        bool SetText(TextView source, Slice& target) noexcept
        {
            target.size = source.size;
            return document.AddText(source,target.offset,error);
        }
        bool Copy(Value source, uint32_t parent, TextView name) noexcept
        {
            Value item = source;
            while (item.IsValid())
            {
                uint32_t index = 0;
                if (!document.AddNode(item.Type(),parent,index,error)) return false;
                auto& node = document.m_Nodes[index];
                node.boolean = item.Boolean(); node.number = item.Number();
                if (!SetText(SameValue(item,source) ? name : item.Name(),node.name) || !SetText(item.Text(),node.text)) return false;
                if (Value child = item.First(); child.IsValid()) { parent = index; item = child; continue; }
                while (!SameValue(item,source) && !item.Next().IsValid())
                {
                    item = item.Parent();
                    parent = document.m_Nodes[parent].parent;
                }
                if (SameValue(item,source)) break;
                item = item.Next();
            }
            return true;
        }
        bool Add(Seed value, uint32_t parent, TextView name = {}) noexcept
        {
            if (value.kind == SeedKind::Copy) return Copy(value.copy,parent,name);
            Kind kind = Kind::Null;
            switch (value.kind)
            {
            case SeedKind::Boolean: kind = Kind::Boolean; break;
            case SeedKind::Integer: kind = Kind::Number; break;
            case SeedKind::String: kind = Kind::String; break;
            case SeedKind::Array: kind = Kind::Array; break;
            case SeedKind::Object: kind = Kind::Object; break;
            default: break;
            }
            uint32_t index = 0;
            if (!document.AddNode(kind,parent,index,error)) return false;
            auto& node = document.m_Nodes[index];
            node.boolean = value.boolean;
            if (!SetText(name,node.name)) return false;
            if (value.kind == SeedKind::String) return SetText(value.text,node.text);
            if (value.kind == SeedKind::Integer)
            {
                char text[21];
                const auto result = std::to_chars(text,text + sizeof(text),value.integer);
                if (result.ec != std::errc{}) return Fail(error,ErrorCode::Capacity,"JSON integer capacity exceeded");
                node.number = double(value.integer);
                return SetText({text,size_t(result.ptr - text)},node.text);
            }
            return true;
        }
    };

    bool Document::Assign(Seed value, Error& error) noexcept
    {
        error = {};
        Document candidate;
        BuilderState builder{candidate,error};
        if (!builder.Count(value) || !builder.Prepare() || !builder.Add(value,None)) return false;
        Adopt(candidate);
        return true;
    }
    bool Document::MakeObject(const Member* members, size_t count, Error& error) noexcept
    {
        error = {};
        if ((!members && count) || count > size_t(PTRDIFF_MAX) / sizeof(Member))
            return Fail(error,ErrorCode::InvalidInput,"invalid JSON member view");
        Document candidate;
        BuilderState builder{candidate,error};
        if (!builder.Count(Seed::Object())) return false;
        for (size_t index = 0; index < count; ++index)
        {
            for (size_t old = 0; old < index; ++old)
                if (SameText(members[old].name,members[index].name))
                    return Fail(error,ErrorCode::InvalidInput,"duplicate object property");
            if (!builder.Count(members[index].value,members[index].name)) return false;
        }
        if (!builder.Prepare() || !builder.Add(Seed::Object(),None)) return false;
        for (size_t index = 0; index < count; ++index)
            if (!builder.Add(members[index].value,0,members[index].name)) return false;
        Adopt(candidate);
        return true;
    }
    bool Document::Replace(TextView name, Seed replacement, Error& error) noexcept
    {
        error = {};
        if (Root().Type() != Kind::Object || !Root().Find(name).IsValid())
            return Fail(error,ErrorCode::InvalidOperation,"missing JSON property");
        Document candidate;
        BuilderState builder{candidate,error};
        if (!builder.Count(Seed::Object())) return false;
        for (Value item = Root().First(); item.IsValid(); item = item.Next())
            if (!builder.Count(SameText(item.Name(),name) ? replacement : Seed{item},item.Name())) return false;
        if (!builder.Prepare() || !builder.Add(Seed::Object(),None)) return false;
        for (Value item = Root().First(); item.IsValid(); item = item.Next())
            if (!builder.Add(SameText(item.Name(),name) ? replacement : Seed{item},0,item.Name())) return false;
        Adopt(candidate);
        return true;
    }
    bool Document::Append(Seed value, Error& error) noexcept
    {
        error = {};
        if (Root().Type() != Kind::Array)
            return Fail(error,ErrorCode::InvalidOperation,"JSON value is not an array");
        // stage the added subtree first, including any borrow from this owner.
        // reserve both arrays before changing links so failed growth keeps views.
        Document added;
        if (!added.Assign(value,error)) return false;
        const size_t requiredNodes = size_t(m_Count) + added.m_Count;
        size_t requiredText = m_TextSize;
        if (!AddSize(requiredText,added.m_TextSize,error)) return false;
        const size_t nodeLimit = size_t(PTRDIFF_MAX) / sizeof(Node) < UINT32_MAX ?
            size_t(PTRDIFF_MAX) / sizeof(Node) : UINT32_MAX;
        size_t nodeCapacity = Growth(m_Capacity,requiredNodes,nodeLimit);
        size_t textCapacity = requiredText ? Growth(m_TextCapacity,requiredText,size_t(PTRDIFF_MAX)) : 0;
        if (!nodeCapacity || (requiredText && !textCapacity))
            return Fail(error,ErrorCode::Capacity,"JSON storage capacity exceeded");
        if (textCapacity > size_t(PTRDIFF_MAX) - nodeCapacity * sizeof(Node))
        {
            if (nodeCapacity > m_Capacity) nodeCapacity = requiredNodes;
            if (textCapacity > m_TextCapacity) textCapacity = requiredText;
            if (textCapacity > size_t(PTRDIFF_MAX) - nodeCapacity * sizeof(Node))
                return Fail(error,ErrorCode::Capacity,"JSON storage capacity exceeded");
        }
        const bool growNodes = nodeCapacity > m_Capacity;
        const bool growText = textCapacity > m_TextCapacity;
        Node* nodes = m_Nodes;
        char* text = m_Text;
        if (growNodes)
        {
            nodes = AllocationFails() ? nullptr : new (std::nothrow) Node[nodeCapacity];
            if (!nodes) return Fail(error,ErrorCode::OutOfMemory,"out of memory growing JSON nodes");
        }
        if (growText)
        {
            text = AllocationFails() ? nullptr : static_cast<char*>(malloc(textCapacity));
            if (!text)
            {
                if (growNodes) delete[] nodes;
                return Fail(error,ErrorCode::OutOfMemory,"out of memory growing JSON text");
            }
        }
        if (growNodes) for (uint32_t index = 0; index < m_Count; ++index) nodes[index] = m_Nodes[index];
        if (growText && m_TextSize) memcpy(text,m_Text,m_TextSize);
        if (added.m_TextSize) memcpy(text + m_TextSize,added.m_Text,added.m_TextSize);
        const uint32_t base = m_Count;
        for (uint32_t index = 0; index < added.m_Count; ++index)
        {
            Node node = added.m_Nodes[index];
            node.parent = node.parent == None ? 0 : node.parent + base;
            if (node.first != None) node.first += base;
            if (node.last != None) node.last += base;
            if (node.next != None) node.next += base;
            node.name.offset += m_TextSize;
            node.text.offset += m_TextSize;
            nodes[base + index] = node;
        }
        if (nodes[0].last == None) nodes[0].first = base;
        else nodes[nodes[0].last].next = base;
        nodes[0].last = base;
        ++nodes[0].count;
        if (growNodes) delete[] m_Nodes;
        if (growText) free(m_Text);
        m_Nodes = nodes; m_Text = text;
        m_Count = uint32_t(requiredNodes); m_Capacity = uint32_t(nodeCapacity);
        m_TextSize = requiredText; m_TextCapacity = textCapacity;
        return true;
    }

    bool Integer(Value value, int64_t& result) noexcept
    {
        if (!value.IsValid() || value.Type() != Kind::Number) return false;
        const TextView token = value.Text();
        int64_t candidate = 0;
        const auto parsed = std::from_chars(token.data,token.data + token.size,candidate);
        if (parsed.ec != std::errc{} || parsed.ptr != token.data + token.size) return false;
        result = candidate;
        return true;
    }
    bool ValidateSigned(Value value, Error& error) noexcept
    {
        error = {};
        if (!value.IsValid()) return Fail(error,ErrorCode::InvalidView,"invalid JSON contract view");
        for (Value item = value; item.IsValid(); item = NextValue(item,value))
        {
            if (item.Type() == Kind::Null) return Fail(error,ErrorCode::InvalidInput,"null is forbidden in a signed contract");
            int64_t integer = 0;
            if (item.Type() == Kind::Number && !Integer(item,integer))
                return Fail(error,ErrorCode::InvalidInput,"contract value is not an int64 integer");
        }
        return true;
    }
    bool Equal(Value left, Value right, NumberComparison numbers) noexcept
    {
        if (!left.IsValid() || !right.IsValid()) return false;
        const Value root = left;
        for (;;)
        {
            if (left.Type() != right.Type() || left.Count() != right.Count()) return false;
            if (left.Type() == Kind::String && !SameText(left.Text(),right.Text())) return false;
            if (left.Type() == Kind::Number)
            {
                if (numbers == NumberComparison::Integer)
                {
                    int64_t a = 0, b = 0;
                    if (!Integer(left,a) || !Integer(right,b) || a != b) return false;
                }
                else if (!SameText(left.Text(),right.Text())) return false;
            }
            if (left.Type() == Kind::Boolean && left.Boolean() != right.Boolean()) return false;
            if (Value child = left.First(); child.IsValid())
            {
                right = left.Type() == Kind::Object ? right.Find(child.Name()) : right.First();
                if (!right.IsValid()) return false;
                left = child;
                continue;
            }
            while (!SameValue(left,root) && !left.Next().IsValid())
            {
                left = left.Parent(); right = right.Parent();
            }
            if (SameValue(left,root)) return true;
            const Value next = left.Next();
            right = left.Parent().Type() == Kind::Object ? right.Parent().Find(next.Name()) : right.Next();
            if (!right.IsValid()) return false;
            left = next;
        }
    }

    namespace
    {
        struct Writer
        {
            char* output;
            size_t capacity;
            Error& error;
            size_t size = 0;
            bool Write(TextView text) noexcept
            {
                if (!text.IsValid()) return Fail(error,ErrorCode::InvalidInput,"invalid JSON text view");
                if (text.size > capacity - size) return Fail(error,ErrorCode::Capacity,"JSON output capacity exceeded");
                if (output && text.size) memcpy(output + size,text.data,text.size);
                size += text.size;
                return true;
            }
            bool Character(char value) noexcept { return Write({&value,1}); }
            bool Escape(TextView text) noexcept
            {
                constexpr char hex[] = "0123456789abcdef";
                if (!text.IsValid()) return Fail(error,ErrorCode::InvalidInput,"invalid JSON text view");
                for (size_t index = 0; index < text.size; ++index)
                {
                    const unsigned char value = static_cast<unsigned char>(text.data[index]);
                    const char* escaped = nullptr;
                    switch (value)
                    {
                    case '"': escaped = "\\\""; break;
                    case '\\': escaped = "\\\\"; break;
                    case '\b': escaped = "\\b"; break;
                    case '\f': escaped = "\\f"; break;
                    case '\n': escaped = "\\n"; break;
                    case '\r': escaped = "\\r"; break;
                    case '\t': escaped = "\\t"; break;
                    default: break;
                    }
                    if (escaped) { if (!Write({escaped,2})) return false; }
                    else if (value < 0x20)
                    {
                        const char unicode[] = {'\\','u','0','0',hex[value >> 4],hex[value & 15]};
                        if (!Write({unicode,sizeof(unicode)})) return false;
                    }
                    else if (!Character(char(value))) return false;
                }
                return true;
            }
            bool Quoted(TextView text) noexcept { return Character('"') && Escape(text) && Character('"'); }
            bool Serialize(Value value) noexcept
            {
                if (!value.IsValid()) return Fail(error,ErrorCode::InvalidView,"invalid JSON serialization view");
                const Value root = value;
                for (;;)
                {
                    switch (value.Type())
                    {
                    case Kind::String: if (!Quoted(value.Text())) return false; break;
                    case Kind::Number: if (!Write(value.Text())) return false; break;
                    case Kind::Boolean: if (!Write(value.Boolean() ? TextView{"true",4} : TextView{"false",5})) return false; break;
                    case Kind::Null: if (!Write({"null",4})) return false; break;
                    case Kind::Array: case Kind::Object:
                        if (!Character(value.Type() == Kind::Object ? '{' : '[')) return false;
                        if (Value child = value.First(); child.IsValid())
                        {
                            if (value.Type() == Kind::Object && (!Quoted(child.Name()) || !Character(':'))) return false;
                            value = child;
                            continue;
                        }
                        if (!Character(value.Type() == Kind::Object ? '}' : ']')) return false;
                        break;
                    }
                    for (;;)
                    {
                        if (SameValue(value,root)) return true;
                        if (Value next = value.Next(); next.IsValid())
                        {
                            if (!Character(',')) return false;
                            if (next.Parent().Type() == Kind::Object && (!Quoted(next.Name()) || !Character(':'))) return false;
                            value = next;
                            break;
                        }
                        value = value.Parent();
                        if (!Character(value.Type() == Kind::Object ? '}' : ']')) return false;
                    }
                }
            }
        };
        bool Writable(char* output, size_t capacity, size_t required, Error& error) noexcept
        {
            if ((!output && capacity) || capacity > size_t(PTRDIFF_MAX) || required >= capacity)
                return Fail(error,ErrorCode::Capacity,"JSON output capacity exceeded");
            return true;
        }
    }
    bool OutputWriter::Raw(TextView value) noexcept
    {
        if (m_Failure.code != ErrorCode::None) return false;
        Writer writer{m_Data ? m_Data + m_Size : nullptr, m_Capacity - m_Size, m_Failure};
        if (!writer.Write(value)) return false;
        m_Size += writer.size;
        return true;
    }

    bool OutputWriter::String(TextView value) noexcept
    {
        if (m_Failure.code != ErrorCode::None) return false;
        Writer writer{m_Data ? m_Data + m_Size : nullptr, m_Capacity - m_Size, m_Failure};
        if (!writer.Quoted(value)) return false;
        m_Size += writer.size;
        return true;
    }

    bool OutputWriter::StringParts(const TextView* parts, size_t count) noexcept
    {
        if (m_Failure.code != ErrorCode::None) return false;
        if ((!parts && count) || count > size_t(PTRDIFF_MAX) / sizeof(TextView))
            return Fail(m_Failure, ErrorCode::InvalidInput, "invalid JSON text view");
        Writer writer{m_Data ? m_Data + m_Size : nullptr, m_Capacity - m_Size, m_Failure};
        if (!writer.Character('"')) return false;
        for (size_t index = 0; index < count; ++index)
            if (!writer.Escape(parts[index])) return false;
        if (!writer.Character('"')) return false;
        m_Size += writer.size;
        return true;
    }

    bool OutputWriter::Integer(int64_t value) noexcept
    {
        char text[32];
        const auto result = std::to_chars(text, text + sizeof(text), value);
        if (result.ec != std::errc{}) return Reject(ErrorCode::InvalidInput, "JSON integer formatting failed");
        return Raw({text, size_t(result.ptr - text)});
    }

    bool OutputWriter::Unsigned(uint64_t value) noexcept
    {
        char text[32];
        const auto result = std::to_chars(text, text + sizeof(text), value);
        if (result.ec != std::errc{}) return Reject(ErrorCode::InvalidInput, "JSON unsigned formatting failed");
        return Raw({text, size_t(result.ptr - text)});
    }

    bool OutputWriter::Reject(ErrorCode code, const char* message) noexcept
    {
        if (m_Failure.code == ErrorCode::None)
            m_Failure = {code == ErrorCode::None ? ErrorCode::InvalidOperation : code, m_Size, message};
        return false;
    }

    EncodedText::EncodedText(OutputEmitter emit, const void* context) noexcept
    {
        if (!emit)
        {
            m_Failure = {ErrorCode::InvalidInput, 0, "JSON output emitter is missing"};
            return;
        }
        OutputWriter measure;
        if (!emit(measure, context) || measure.Failure().code != ErrorCode::None)
        {
            m_Failure = measure.Failure();
            if (m_Failure.code == ErrorCode::None)
                m_Failure = {ErrorCode::InvalidOperation, 0, "JSON output measurement failed"};
            return;
        }
        char* candidate = AllocationFails() ? nullptr : new (std::nothrow) char[measure.Size() + 1];
        if (!candidate)
        {
            m_Failure = {ErrorCode::OutOfMemory, 0, "JSON output allocation failed"};
            return;
        }
        OutputWriter writer(candidate, measure.Size());
        const bool emitted = emit(writer, context);
        if (!emitted || writer.Failure().code != ErrorCode::None || writer.Size() != measure.Size())
        {
            m_Failure = writer.Failure();
            if (m_Failure.code == ErrorCode::None)
                m_Failure = {ErrorCode::InvalidOperation, writer.Size(), "JSON output changed between passes"};
            delete[] candidate;
            return;
        }
        candidate[writer.Size()] = '\0';
        m_Data = candidate;
        m_Size = writer.Size();
        m_Failure = {};
    }

    EncodedText::~EncodedText() noexcept { delete[] m_Data; }

    EncodedText::EncodedText(EncodedText&& source) noexcept
    {
        *this = static_cast<EncodedText&&>(source);
    }

    EncodedText& EncodedText::operator=(EncodedText&& source) noexcept
    {
        if (this == &source) return *this;
        delete[] m_Data;
        m_Data = source.m_Data;
        m_Size = source.m_Size;
        m_Failure = source.m_Failure;
        source.m_Data = nullptr;
        source.m_Size = 0;
        source.m_Failure = {ErrorCode::InvalidOperation, 0, "JSON output was moved"};
        return *this;
    }

    bool MeasureEscaped(TextView input, size_t& size, Error& error) noexcept
    {
        error = {};
        Writer writer{nullptr,size_t(PTRDIFF_MAX),error};
        if (!writer.Escape(input)) return false;
        size = writer.size;
        return true;
    }
    bool WriteEscaped(TextView input, char* output, size_t capacity, size_t& size, Error& error) noexcept
    {
        size_t required = 0;
        if (!MeasureEscaped(input,required,error) || !Writable(output,capacity,required,error)) return false;
        if (Overlaps(input,output,capacity)) return Fail(error,ErrorCode::InvalidInput,"JSON output overlaps its input");
        Writer writer{output,capacity,error};
        if (!writer.Escape(input)) return false;
        output[writer.size] = '\0'; size = writer.size;
        return true;
    }
    bool MeasureSerialized(Value input, size_t& size, Error& error) noexcept
    {
        error = {};
        Writer writer{nullptr,size_t(PTRDIFF_MAX),error};
        if (!writer.Serialize(input)) return false;
        size = writer.size;
        return true;
    }
    bool WriteSerialized(Value input, char* output, size_t capacity, size_t& size, Error& error) noexcept
    {
        size_t required = 0;
        if (!MeasureSerialized(input,required,error) || !Writable(output,capacity,required,error)) return false;
        if (Overlaps({input.owner->m_Text,input.owner->m_TextSize},output,capacity))
            return Fail(error,ErrorCode::InvalidInput,"JSON output overlaps its input");
        Writer writer{output,capacity,error};
        if (!writer.Serialize(input)) return false;
        output[writer.size] = '\0'; size = writer.size;
        return true;
    }
#if defined(UVSR_JSON_TEST_HOOKS)
    void FailAllocationAfter(size_t count) noexcept { allocationsUntilFailure = count; }
    void ClearAllocationFailure() noexcept { allocationsUntilFailure = SIZE_MAX; }
#endif
}
