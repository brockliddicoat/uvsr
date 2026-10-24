#include "renderer_import.h"
#include <math.h>
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    using namespace uvsr;
    size_t rejected = 0;
    void Require(bool value, const char* reason)
    {
        if (value) return;
        fprintf(stderr, "import accessor check failed: %s\n", reason);
        exit(1);
    }
    void Good(ImportResult result, const char* reason)
    {
        if (result) return;
        fprintf(stderr, "import accessor check failed: %s: %s, object %u, index %zu, parser %u\n",
            reason, ImportErrorText(result.error), unsigned(result.object), result.index, result.parserCode);
        exit(1);
    }
    void Bad(ImportResult result, ImportError error, const char* reason)
    {
        Require(result.error == error, reason);
        ++rejected;
    }
    ArrayView<const uint8_t> Bytes(const char* text) { return {reinterpret_cast<const uint8_t*>(text), strlen(text)}; }
    void Parse(ImportDocument& document, const char* json) { Good(document.Parse(Bytes(json)), "parse fixture"); }
    void Put32(uint8_t* bytes, uint32_t value)
    {
        for (size_t i = 0; i < 4; ++i) bytes[i] = uint8_t(value >> (i * 8));
    }

    void BasicOwnership()
    {
        ImportDocument document;
        const char* json = R"({"asset":{"version":"2.0"},"buffers":[{"uri":"geometry%20data.bin","byteLength":28}],"bufferViews":[{"buffer":0,"byteLength":28,"byteStride":16}],"accessors":[{"bufferView":0,"componentType":5126,"count":2,"type":"VEC3"}]})";
        char* source = static_cast<char*>(malloc(strlen(json) + 1));
        Require(source != nullptr, "fixture allocation");
        memcpy(source, json, strlen(json) + 1);
        Parse(document, source);
        memset(source, 0xcc, strlen(json));
        free(source);
        ImportBufferInfo buffer;
        Good(document.BufferInfo(0, buffer), "buffer metadata");
        Require(buffer.byteCount == 28 && !buffer.resident && buffer.uri.count > 0, "external buffer remains unresolved");
        Require(buffer.uri.count == strlen("geometry data.bin") &&
            memcmp(buffer.uri.data,"geometry data.bin",buffer.uri.count)==0,"path decoded exactly once");
        float output[6]{91,92,93,94,95,96};
        Bad(document.ReadFloats(0, output), ImportError::BufferUnavailable, "unresolved read");
        Require(output[0] == 91 && output[5] == 96, "unresolved output unchanged");
        float input[]{1,-2,3,999,4,5,-6};
        Good(document.SupplyBuffer(0, {reinterpret_cast<const uint8_t*>(input), sizeof(input)}), "owned external copy");
        memset(input, 0xdd, sizeof(input));
        Good(document.ReadFloats(0, output), "strided float values");
        const float expected[]{1,-2,3,4,5,-6};
        Require(memcmp(output, expected, sizeof(output)) == 0, "strided values preserved after caller destruction");
        ImportAccessorInfo info;
        Good(document.AccessorInfo(0, info), "accessor metadata");
        Require(info.count == 2 && info.scalarCount == 6 && info.shape == ImportShape::Vec3, "plain metadata");
        Bad(document.ReadFloats(0, {output,5}), ImportError::InvalidOutput, "short output");
        Bad(document.ReadFloats(0, {nullptr,6}), ImportError::InvalidOutput, "null output");
        alignas(float) uint8_t misaligned[sizeof(output) + 1]{};
        Bad(document.ReadFloats(0, {reinterpret_cast<float*>(misaligned + 1),6}), ImportError::InvalidOutput, "unaligned output");
        Bad(document.ReadFloats(1, output), ImportError::InvalidIndex, "bad accessor index");
        Bad(document.SupplyBuffer(0, {reinterpret_cast<const uint8_t*>(input),4}), ImportError::InvalidRange, "short external buffer");
        Bad(document.Parse(Bytes("{")), ImportError::InvalidJson, "parse failure preserves old document");
        Good(document.ReadFloats(0, output), "prior document still readable");
        Require(memcmp(output, expected, sizeof(output)) == 0, "failure preserves prior values");
        ImportDocument moved(static_cast<ImportDocument&&>(document));
        Require(document.BufferCount() == 0 && moved.AccessorCount() == 1, "move ownership");
        document = static_cast<ImportDocument&&>(moved);
        Require(moved.BufferCount() == 0, "move assignment ownership");
        Good(document.ReadFloats(0, output), "moved values");
        document.Reset();
        document.Reset();
        Require(document.AccessorCount() == 0 && document.BufferCount() == 0, "idempotent reset");
    }

    void IntegerConversion()
    {
        const struct Case { unsigned type; uint32_t first; uint32_t second; size_t size; float expectedFirst; float expectedSecond; } cases[]{
            {5120,128,127,1,-1,1}, {5121,0,255,1,0,1},
            {5122,32768,32767,2,-1,1}, {5123,0,65535,2,0,1}
        };
        for (const auto& value : cases)
        {
            char json[512];
            snprintf(json,sizeof(json),R"({"asset":{"version":"2.0"},"buffers":[{"uri":"data.bin","byteLength":%zu}],"bufferViews":[{"buffer":0,"byteLength":%zu}],"accessors":[{"bufferView":0,"componentType":%u,"normalized":true,"count":1,"type":"VEC2"}]})", value.size*2,value.size*2,value.type);
            ImportDocument document;
            Parse(document,json);
            uint8_t data[4]{};
            for (size_t i=0;i<value.size;++i) { data[i]=uint8_t(value.first>>(i*8)); data[value.size+i]=uint8_t(value.second>>(i*8)); }
            Good(document.SupplyBuffer(0,{data,value.size*2}),"normalized bytes");
            float output[2];
            Good(document.ReadFloats(0,output),"normalized conversion");
            Require(output[0]==value.expectedFirst && output[1]==value.expectedSecond,"normalized endpoints");
            uint32_t integers[2]{77,88};
            Bad(document.ReadUnsigned(0,integers),ImportError::InvalidAccessor,"normalized integer output rejected");
            Require(integers[0]==77 && integers[1]==88,"rejected conversion unchanged");
        }
        const unsigned unsignedTypes[]{5121,5123,5125};
        const size_t sizes[]{1,2,4};
        const uint32_t maxima[]{255,65535,UINT32_MAX};
        for (size_t i=0;i<3;++i)
        {
            char json[512];
            snprintf(json,sizeof(json),R"({"asset":{"version":"2.0"},"buffers":[{"uri":"i.bin","byteLength":%zu}],"bufferViews":[{"buffer":0,"byteLength":%zu}],"accessors":[{"bufferView":0,"componentType":%u,"count":1,"type":"SCALAR"}]})",sizes[i],sizes[i],unsignedTypes[i]);
            ImportDocument document;
            Parse(document,json);
            const uint8_t bytes[]{255,255,255,255};
            Good(document.SupplyBuffer(0,{bytes,sizes[i]}),"unsigned bytes");
            uint32_t output;
            Good(document.ReadUnsigned(0,{&output,1}),"unsigned conversion");
            Require(output==maxima[i],"integer width preserved");
        }
    }

    void Matrices()
    {
        const size_t dimensions[]{2,3,4};
        const size_t sizes[]{1,2,4};
        for (size_t dimension : dimensions)
        for (size_t component : sizes)
        {
            const size_t rowBytes=dimension*component;
            const size_t columnStride=(rowBytes+3)&~size_t(3);
            const size_t stride=columnStride*dimension;
            const size_t bytes=stride*2-(columnStride-rowBytes);
            char json[512];
            snprintf(json,sizeof(json),R"({"asset":{"version":"2.0"},"buffers":[{"uri":"m.bin","byteLength":%zu}],"bufferViews":[{"buffer":0,"byteLength":%zu}],"accessors":[{"bufferView":0,"componentType":%u,"count":2,"type":"MAT%zu"}]})",bytes,bytes,component==1?5121u:component==2?5123u:5126u,dimension);
            ImportDocument document;
            Parse(document,json);
            uint8_t data[128];
            memset(data,0xcd,sizeof(data));
            float expected[32];
            size_t k=0;
            for(size_t matrix=0;matrix<2;++matrix)
            for(size_t column=0;column<dimension;++column)
            for(size_t row=0;row<dimension;++row)
            {
                const uint32_t value=uint32_t(++k);
                expected[k-1]=float(value);
                uint8_t* destination=data+matrix*stride+column*columnStride+row*component;
                if(component==4) { float f=float(value); memcpy(destination,&f,4); }
                else for(size_t byte=0;byte<component;++byte) destination[byte]=uint8_t(value>>(byte*8));
            }
            Good(document.SupplyBuffer(0,{data,bytes}),"matrix padded source");
            float output[32];
            Good(document.ReadFloats(0,{output,k}),"matrix conversion");
            Require(memcmp(output,expected,k*sizeof(float))==0,"column-major values and omitted final padding");
        }
        ImportDocument zero;
        Parse(zero,R"({"asset":{"version":"2.0"},"accessors":[{"componentType":5126,"count":2,"type":"MAT4"}]})");
        float output[32];
        memset(output,0xcc,sizeof(output));
        Good(zero.ReadFloats(0,output),"zero matrix accessor");
        for(float value:output) Require(value==0,"missing matrix base is zero");
    }

    void Sparse()
    {
        const unsigned types[]{5121,5123,5125};
        const size_t widths[]{1,2,4};
        for(size_t kind=0;kind<3;++kind)
        for(unsigned base=0;base<2;++base)
        {
            char json[1024];
            snprintf(json,sizeof(json),R"({"asset":{"version":"2.0"},"buffers":[{"uri":"s.bin","byteLength":64}],"bufferViews":[{"buffer":0,"byteLength":24,"byteStride":8},{"buffer":0,"byteOffset":24,"byteLength":8},{"buffer":0,"byteOffset":32,"byteLength":8}],"accessors":[{%s"componentType":5126,"count":3,"type":"SCALAR","sparse":{"count":2,"indices":{"bufferView":1,"componentType":%u},"values":{"bufferView":2}}}]})",base?"\"bufferView\":0,":"",types[kind]);
            ImportDocument document;
            Parse(document,json);
            uint8_t bytes[64]{};
            const float values[]{10,11,12,70,90};
            for(size_t i=0;i<3;++i) memcpy(bytes+i*8,&values[i],4);
            Put32(bytes+24,0);
            for(size_t i=0;i<widths[kind];++i) bytes[24+widths[kind]+i]=uint8_t(2u>>(i*8));
            memcpy(bytes+32,values+3,8);
            Good(document.SupplyBuffer(0,bytes),"sparse bytes");
            float output[3];
            Good(document.ReadFloats(0,output),"sparse conversion");
            Require(output[0]==70 && output[1]==(base?11.0f:0.0f) && output[2]==90,"sparse merged values");
            bytes[24+widths[kind]]=0;
            Good(document.SupplyBuffer(0,bytes),"duplicate sparse bytes");
            Bad(document.ReadFloats(0,output),ImportError::InvalidSparse,"duplicate sparse index");
            Require(output[0]==70 && output[2]==90,"invalid sparse output unchanged");
            bytes[24]=2; bytes[24+widths[kind]]=1;
            Good(document.SupplyBuffer(0,bytes),"descending sparse bytes");
            Bad(document.ReadFloats(0,output),ImportError::InvalidSparse,"descending sparse index");
            bytes[24]=0; bytes[24+widths[kind]]=3;
            Good(document.SupplyBuffer(0,bytes),"out of range sparse bytes");
            Bad(document.ReadFloats(0,output),ImportError::InvalidSparse,"sparse index at count");
        }
        ImportDocument document;
        Parse(document,R"({"asset":{"version":"2.0"},"buffers":[{"uri":"s.bin","byteLength":30}],"bufferViews":[{"buffer":0,"byteLength":1},{"buffer":0,"byteOffset":6,"byteLength":24}],"accessors":[{"componentType":5123,"normalized":true,"count":2,"type":"MAT3","sparse":{"count":1,"indices":{"bufferView":0,"componentType":5121},"values":{"bufferView":1,"byteOffset":2}}}]})");
        uint8_t matrix[30]{};
        matrix[0]=1;
        for(size_t col=0;col<3;++col)
        for(size_t row=0;row<3;++row)
            matrix[8+col*8+row*2]=matrix[9+col*8+row*2]=255;
        Good(document.SupplyBuffer(0,matrix),"sparse matrix with compensated start alignment");
        float output[18];
        Good(document.ReadFloats(0,output),"normalized sparse matrix");
        for(size_t i=0;i<18;++i) Require(output[i]==(i<9?0.0f:1.0f),"zero base and normalized matrix replacement");
    }

    void EmbeddedAndBinary()
    {
        ImportDocument document;
        Parse(document,R"({"asset":{"version":"2.0"},"buffers":[{"uri":"data:application/octet-stream;base64,AAECAw==","byteLength":4}],"bufferViews":[{"buffer":0,"byteLength":4}],"accessors":[{"bufferView":0,"componentType":5121,"count":4,"type":"SCALAR"}]})");
        uint32_t values[4];
        Good(document.ReadUnsigned(0,values),"base64 owned data");
        for(size_t i=0;i<4;++i) Require(values[i]==i,"base64 decode values");
        const char* json=R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":4}],"bufferViews":[{"buffer":0,"byteLength":4}],"accessors":[{"bufferView":0,"componentType":5121,"count":4,"type":"SCALAR"}]})";
        uint8_t glb[512]{};
        const size_t jsonSize=(strlen(json)+3)&~size_t(3);
        const size_t length=12+8+jsonSize+8+4;
        Put32(glb,0x46546c67); Put32(glb+4,2); Put32(glb+8,uint32_t(length));
        Put32(glb+12,uint32_t(jsonSize)); Put32(glb+16,0x4e4f534a);
        memset(glb+20,' ',jsonSize); memcpy(glb+20,json,strlen(json));
        Put32(glb+20+jsonSize,4); Put32(glb+24+jsonSize,0x004e4942);
        for(size_t i=0;i<4;++i) glb[length-4+i]=uint8_t(10+i);
        Good(document.Parse({glb,length}),"GLB container");
        memset(glb,0xee,length);
        Good(document.ReadUnsigned(0,values),"GLB storage survives input overwrite");
        for(size_t i=0;i<4;++i) Require(values[i]==10+i,"GLB values");

        json=R"({"asset":{"version":"2.0"},"accessors":[{"componentType":5126,"count":1,"type":"SCALAR"}]})";
        const size_t onlyJsonSize=(strlen(json)+3)&~size_t(3);
        const size_t onlyJsonLength=20+onlyJsonSize;
        Put32(glb,0x46546c67); Put32(glb+4,2); Put32(glb+8,uint32_t(onlyJsonLength));
        Put32(glb+12,uint32_t(onlyJsonSize)); Put32(glb+16,0x4e4f534a);
        memset(glb+20,' ',onlyJsonSize); memcpy(glb+20,json,strlen(json));
        Good(document.Parse({glb,onlyJsonLength}),"JSON-only GLB");
        Put32(glb+8,uint32_t(onlyJsonLength+12));
        Put32(glb+onlyJsonLength,4); Put32(glb+onlyJsonLength+4,0x11223344);
        Good(document.Parse({glb,onlyJsonLength+12}),"GLB ignores unknown trailing chunk");
        float zero=7;
        Good(document.ReadFloats(0,{&zero,1}),"JSON-only GLB data");
        Require(zero==0,"JSON-only GLB retains accessor");
    }

    void Failures()
    {
        ImportDocument document;
        const struct { const char* json; ImportError error; } cases[]{
            {R"({"asset":{"version":"1.0"}})",ImportError::UnsupportedVersion},
            {R"({"asset":{"version":""}})",ImportError::InvalidData},
            {R"({"asset":{"version":"2bad"}})",ImportError::InvalidData},
            {R"({"asset":{"version":"2.0.1"}})",ImportError::InvalidData},
            {R"({"asset":{"version":"2.0","minVersion":"2.1"}})",ImportError::UnsupportedVersion},
            {R"({"asset":{"version":"2.0"},"extensionsRequired":{}})",ImportError::InvalidData},
            {R"({"asset":{"version":"2.0"},"extensionsRequired":[1]})",ImportError::InvalidData},
            {R"({"asset":{"version":"2.0"},"extensionsRequired":["UVSR_unknown"]})",ImportError::UnsupportedExtension},
            {R"({"asset":{"version":"2.0"},"accessors":[{"componentType":5126,"count":0,"type":"SCALAR"}]})",ImportError::InvalidAccessor},
            {R"({"asset":{"version":"2.0"},"accessors":[{"componentType":5126,"count":18446744073709551615,"type":"MAT4"}]})",ImportError::Overflow},
            {R"({"asset":{"version":"2.0"},"accessors":[{"componentType":4294972422,"count":1,"type":"SCALAR"}]})",ImportError::InvalidAccessor},
            {R"({"asset":{"version":"2.0"},"accessors":[{"componentType":5121,"count":1,"type":"SCALAR","min":[1e100]}]})",ImportError::InvalidAccessor},
            {R"({"asset":{"version":"2.0"},"accessors":[{"componentType":5126,"count":1,"type":"SCALAR","bufferView":-1}]})",ImportError::InvalidAccessor},
            {R"({"asset":{"version":"2.0"},"accessors":[{"componentType":5126,"count":1,"type":"SCALAR","bufferView":0}]})",ImportError::InvalidIndex},
            {R"({"asset":{"version":"2.0"},"buffers":[{"uri":"b.bin","byteLength":8}],"bufferViews":[{"buffer":1,"byteLength":8}]})",ImportError::InvalidIndex},
            {R"({"asset":{"version":"2.0"},"buffers":[{"uri":"b.bin","byteLength":8}],"bufferViews":[{"buffer":0,"byteOffset":18446744073709551615,"byteLength":8}]})",ImportError::InvalidRange},
            {R"({"asset":{"version":"2.0"},"buffers":[{"uri":"b.bin","byteLength":8}],"bufferViews":[{"buffer":0,"byteLength":8,"byteStride":3}]})",ImportError::InvalidRange},
            {R"({"asset":{"version":"2.0"},"buffers":[{"uri":"b.bin","byteLength":8}],"bufferViews":[{"buffer":0,"byteLength":8}],"accessors":[{"bufferView":0,"byteOffset":1,"componentType":5126,"count":1,"type":"SCALAR"}]})",ImportError::InvalidRange},
            {R"({"asset":{"version":"2.0"},"buffers":[{"uri":"b.bin","byteLength":8}],"bufferViews":[{"buffer":0,"byteLength":8}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"SCALAR"}]})",ImportError::InvalidRange},
            {R"({"asset":{"version":"2.0"},"buffers":[{"uri":"data:application/octet-stream;base64,AA==","byteLength":2}]})",ImportError::InvalidRange}
        };
        for(const auto& value:cases) Bad(document.Parse(Bytes(value.json)),value.error,"malformed descriptor rejected");
        const char* encoded[]{"", "A", "AAA", "AAAAA", "====", "AA?=", "A===", "=AAA", "AAA\n"};
        for(const char* value:encoded)
        {
            char json[512];
            snprintf(json,sizeof(json),R"({"asset":{"version":"2.0"},"buffers":[{"uri":"data:application/octet-stream;base64,%s","byteLength":1}]})",value);
            Require(!document.Parse(Bytes(json)),"malformed base64 rejected"); ++rejected;
        }
        const char* uris[]{"", "//", "//server", ":bad", "b%", "b%0", "b%QX", "b%00.bin", "%2F%2F", "%3Abad", "file:%2F%2F"};
        for(const char* value:uris)
        {
            char json[512];
            snprintf(json,sizeof(json),R"({"asset":{"version":"2.0"},"buffers":[{"uri":"%s","byteLength":1}]})",value);
            Bad(document.Parse(Bytes(json)),ImportError::InvalidUri,"malformed URI rejected");
        }
        const char* validUris[]{"file:///C:/media/b.bin", "b%23part.bin", "b%2520part.bin", "folder/grande_sph%C3%A8re.bin"};
        for(const char* value:validUris)
        {
            char json[512];
            snprintf(json,sizeof(json),R"({"asset":{"version":"2.0"},"buffers":[{"uri":"%s","byteLength":1}]})",value);
            Parse(document,json);
        }
        uint8_t glb[24]{};
        Put32(glb,0x46546c67); Put32(glb+4,2);
        for(size_t length=4;length<20;++length)
        {
            Put32(glb+8,uint32_t(length));
            Bad(document.Parse({glb,length}),ImportError::InvalidContainer,"truncated GLB header");
        }
        Put32(glb+8,24); Put32(glb+12,UINT32_MAX); Put32(glb+16,0x4e4f534a);
        Bad(document.Parse(glb),ImportError::InvalidContainer,"GLB chunk overflow");
        uint8_t dummy=0;
        Bad(document.Parse({&dummy,SIZE_MAX}),ImportError::Overflow,"input padding overflow");
        Bad(document.Parse({nullptr,1}),ImportError::InvalidInput,"null input");

        Parse(document,R"({"asset":{"version":"2.0"},"buffers":[{"uri":"f.bin","byteLength":8}],"bufferViews":[{"buffer":0,"byteLength":8}],"accessors":[{"bufferView":0,"componentType":5126,"count":2,"type":"SCALAR"}]})");
        const uint32_t invalidFloats[]{0x7f800000,0xff800000,0x7fc00000};
        for(uint32_t bits:invalidFloats)
        {
            float data[]{1,0}; memcpy(data+1,&bits,4);
            Good(document.SupplyBuffer(0,{reinterpret_cast<const uint8_t*>(data),sizeof(data)}),"nonfinite fixture bytes");
            float output[]{71,72};
            Bad(document.ReadFloats(0,output),ImportError::NonFiniteValue,"nonfinite rejected");
            Require(output[0]==71 && output[1]==72,"nonfinite output unchanged");
        }
    }

    void SparseLayoutFailures()
    {
        const struct { const char* fields; ImportError error; } cases[]{
            {R"("count":0,"indices":{"bufferView":0,"componentType":5121},"values":{"bufferView":1})",ImportError::InvalidSparse},
            {R"("count":4,"indices":{"bufferView":0,"componentType":5121},"values":{"bufferView":1})",ImportError::InvalidSparse},
            {R"("count":1,"indices":{"bufferView":4,"componentType":5121},"values":{"bufferView":1})",ImportError::InvalidIndex},
            {R"("count":1,"indices":{"bufferView":0,"componentType":5121},"values":{"bufferView":9})",ImportError::InvalidIndex},
            {R"("count":1,"indices":{"bufferView":0,"componentType":5120},"values":{"bufferView":1})",ImportError::InvalidSparse},
            {R"("count":1,"indices":{"bufferView":0,"componentType":4294972417},"values":{"bufferView":1})",ImportError::InvalidSparse},
            {R"("count":1,"indices":{"bufferView":0,"componentType":5123,"byteOffset":1},"values":{"bufferView":1})",ImportError::InvalidRange},
            {R"("count":1,"indices":{"bufferView":0,"componentType":5121,"byteOffset":18446744073709551615},"values":{"bufferView":1})",ImportError::InvalidRange},
            {R"("count":1,"indices":{"bufferView":0,"componentType":5121},"values":{"bufferView":1,"byteOffset":1})",ImportError::InvalidRange},
            {R"("count":3,"indices":{"bufferView":0,"componentType":5121},"values":{"bufferView":1,"byteOffset":8})",ImportError::InvalidRange},
            {R"("count":1,"indices":{"bufferView":2,"componentType":5121},"values":{"bufferView":1})",ImportError::InvalidSparse}
        };
        for(const auto& value:cases)
        {
            char json[1024];
            snprintf(json,sizeof(json),R"({"asset":{"version":"2.0"},"buffers":[{"uri":"s.bin","byteLength":64}],"bufferViews":[{"buffer":0,"byteLength":4},{"buffer":0,"byteOffset":4,"byteLength":16},{"buffer":0,"byteOffset":20,"byteLength":4,"target":34962}],"accessors":[{"componentType":5126,"count":3,"type":"SCALAR","sparse":{%s}}]})",value.fields);
            ImportDocument document;
            Bad(document.Parse(Bytes(json)),value.error,"invalid sparse layout rejected");
        }
        char json[4096];
        const char* prefix=R"({"asset":{"version":"2.0"},"extras":)";
        const size_t depths[]{1000,1100};
        for(size_t depth:depths)
        {
            size_t offset=strlen(prefix); memcpy(json,prefix,offset);
            for(size_t i=0;i<depth;++i) json[offset++]='[';
            json[offset++]='0';
            for(size_t i=0;i<depth;++i) json[offset++]=']';
            json[offset++]='}'; json[offset]=0;
            ImportDocument document;
            if(depth==1000) Parse(document,json);
            else Bad(document.Parse(Bytes(json)),ImportError::InvalidJson,"vendor JSON nesting bound");
        }
    }

    uint8_t* ReadFile(const char* path, size_t& size)
    {
        FILE* file=nullptr;
#if defined(_MSC_VER)
        fopen_s(&file,path,"rb");
#else
        file=fopen(path,"rb");
#endif
        Require(file!=nullptr,"open scene fixture input");
        Require(fseek(file,0,SEEK_END)==0,"seek fixture end");
        const long length=ftell(file);
        Require(length>=0 && fseek(file,0,SEEK_SET)==0,"seek fixture start");
        size=size_t(length);
        auto* bytes=static_cast<uint8_t*>(malloc(size?size:1));
        Require(bytes && fread(bytes,1,size,file)==size,"read scene fixture bytes");
        Require(fclose(file)==0,"close scene fixture input");
        return bytes;
    }

    void ShippedAccessors(const char* path)
    {
        size_t size=0;
        auto* bytes=ReadFile(path,size);
        ImportDocument document;
        Good(document.Parse({bytes,size}),"parse shipped input");
        free(bytes);
        const char* slash=strrchr(path,'/');
        Require(slash!=nullptr,"fixture path uses slashes");
        const size_t directory=size_t(slash-path)+1;
        uint64_t inputBytes=0;
        for(size_t i=0;i<document.BufferCount();++i)
        {
            ImportBufferInfo info;
            Good(document.BufferInfo(i,info),"shipped buffer metadata");
            if(info.resident) continue;
            char filename[4096];
            Require(directory+info.uri.count<sizeof(filename),"fixture path capacity");
            memcpy(filename,path,directory); memcpy(filename+directory,info.uri.data,info.uri.count);
            filename[directory+info.uri.count]=0;
            bytes=ReadFile(filename,size);
            Good(document.SupplyBuffer(i,{bytes,size}),"shipped external buffer copy");
            inputBytes+=size;
            free(bytes);
        }
        uint64_t scalarCount=0;
        uint64_t checksum=14695981039346656037ull;
        for(size_t i=0;i<document.AccessorCount();++i)
        {
            ImportAccessorInfo info;
            Good(document.AccessorInfo(i,info),"shipped accessor metadata");
            const size_t byteCount=info.scalarCount*sizeof(float);
            auto* values=malloc(byteCount);
            Require(values!=nullptr,"shipped fixture output allocation");
            const bool integers=!info.normalized && info.shape<=ImportShape::Vec4 &&
                (info.component==ImportComponent::Uint8 || info.component==ImportComponent::Uint16 || info.component==ImportComponent::Uint32);
            for(size_t scalar=0;scalar<info.scalarCount;++scalar)
            {
                if(integers) new (static_cast<uint32_t*>(values)+scalar) uint32_t;
                else new (static_cast<float*>(values)+scalar) float;
            }
            if(integers) Good(document.ReadUnsigned(i,{static_cast<uint32_t*>(values),info.scalarCount}),"shipped integer accessor");
            else Good(document.ReadFloats(i,{static_cast<float*>(values),info.scalarCount}),"shipped float accessor");
            for(size_t byte=0;byte<byteCount;++byte) checksum=(checksum^static_cast<const uint8_t*>(values)[byte])*1099511628211ull;
            scalarCount+=info.scalarCount;
            free(values);
        }
        printf("shipped accessors: %s, %zu buffers, %llu bytes, %zu accessors, %llu scalars, FNV1a64 %016llx\n",
            path,document.BufferCount(),static_cast<unsigned long long>(inputBytes),document.AccessorCount(),
            static_cast<unsigned long long>(scalarCount),static_cast<unsigned long long>(checksum));
    }

    void AllocationFailures()
    {
        const char* json=R"({"asset":{"version":"2.0"},"buffers":[{"uri":"a.bin","byteLength":1}],"bufferViews":[{"buffer":0,"byteLength":1}],"accessors":[{"bufferView":0,"componentType":5121,"count":1,"type":"SCALAR"}]})";
        ImportDocument document;
        Parse(document,json);
        uint8_t byte=17;
        Good(document.SupplyBuffer(0,{&byte,1}),"baseline bytes");
        for(int64_t i=0;i<2;++i)
        {
            SetImportAllocationFailureCountdown(i);
            Bad(document.Parse(Bytes(json)),ImportError::OutOfMemory,"checked parser owner/table allocation");
            SetImportAllocationFailureCountdown(-1);
            uint32_t output;
            Good(document.ReadUnsigned(0,{&output,1}),"old data after allocation failure");
            Require(output==17,"old data preserved");
        }
        byte=23;
        SetImportAllocationFailureCountdown(0);
        Bad(document.SupplyBuffer(0,{&byte,1}),ImportError::OutOfMemory,"checked external byte allocation");
        SetImportAllocationFailureCountdown(-1);
        uint32_t output;
        Good(document.ReadUnsigned(0,{&output,1}),"old external buffer retained");
        Require(output==17,"supply failure preserves old bytes");
        Good(document.SupplyBuffer(0,{&byte,1}),"retry external allocation");
        Good(document.ReadUnsigned(0,{&output,1}),"retry read");
        Require(output==23,"retry succeeds");
        Parse(document,json);
    }
}

int main(int argc, char** argv)
{
    if(argc>1)
    {
        for(int i=1;i<argc;++i) ShippedAccessors(argv[i]);
        return 0;
    }
    BasicOwnership(); IntegerConversion(); Matrices(); Sparse(); EmbeddedAndBinary(); Failures(); SparseLayoutFailures(); AllocationFailures();
    printf("import accessors: owned JSON/GLB/base64/external bytes, strided/normalized/sparse/matrix conversion, %zu explicit rejections, three allocation faults and retry passed\n",rejected);
}
