#include "scene_catalog.h"
#include "scene_catalog_path.h"
#include "settings_snapshot_storage.h"
#include "file_bytes.h"
#include <Windows.h>
#include <clocale>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace
{
    size_t Assertions = 0;
    void Require(bool value, const char* message)
    {
        ++Assertions;
        if (!value) throw std::runtime_error(message);
    }
    void ClearFailures()
    {
        uvsr::ClearSceneCatalogAllocationFailure();
        uvsr::json::ClearAllocationFailure();
        uvsr::ClearFileAllocationFailure();
        uvsr::ClearFileReadTestLimits();
    }
    std::string Snapshot(const uvsr::SceneCatalog& catalog)
    {
        std::string result;
        const auto append = [&](const void* data, size_t size) {
            result.append(reinterpret_cast<const char*>(&size), sizeof(size));
            if (size) result.append(static_cast<const char*>(data), size);
        };
        for (const auto& entry : catalog)
        {
            for (auto value : {entry.FileName,entry.CommandName,entry.DisplayName}) append(value.data(),value.size());
            const bool hasCamera = bool(entry.InitialCamera); append(&hasCamera,sizeof(hasCamera));
            if (entry.InitialCamera)
            {
                const auto& camera = *entry.InitialCamera;
                append(camera.Position.data(),sizeof(float)*3); append(camera.Direction.data(),sizeof(float)*3);
                append(camera.Up.data(),sizeof(float)*3); append(&camera.VerticalFovDegrees,sizeof(float));
            }
        }
        return result;
    }
    void AssignDetail(uvsr::SettingsSnapshotError& error, std::string_view value)
    {
        error.detail = uvsr::json::EncodedText([](uvsr::json::OutputWriter& writer,const void* context) noexcept {
            const auto view = *static_cast<const std::string_view*>(context);
            return writer.Raw({view.data(),view.size()});
        },&value);
        Require(error.detail.IsValid(),"could not prepare owned error input");
    }
}

void TestSceneCatalogOwnership(const std::filesystem::path& root)
{
    using namespace uvsr;
    ClearFailures();
    std::filesystem::create_directories(root);
    const auto descriptor = root / "owner.scene.json";
    const std::string file = descriptor.generic_string(), part = (root / "part.glb").generic_string();
    {
        std::ofstream stream(descriptor,std::ios::binary);
        stream << "{\"displayName\":\"" << std::string(8192,'x') << "\",\"models\":[\"part.glb\"],"
            "\"initialCamera\":{\"position\":[1,2,3],\"direction\":[0,0,-2],\"up\":[0,3,0]}}";
        Require(stream.good(),"could not write catalog ownership fixture");
    }
    const std::string_view discovered[]{file,part};
    const auto seed = [&](SceneCatalog& catalog) {
        ClearFailures(); SettingsSnapshotError error;
        Require(BuildSceneCatalog(root.native(),discovered,catalog,error),"could not seed scene catalog");
        Require(catalog.Count()==1 && catalog[0].InitialCamera && catalog[0].DisplayName.size()==8192,
            "seed catalog lost descriptor metadata");
    };
    size_t catalogFailures=0,jsonFailures=0,fileFailures=0;
    for (unsigned owner=0;owner<3;++owner)
    {
        SceneCatalog published;seed(published);
        const auto* entries=published.Entries();const auto view=published[0].DisplayName;const auto before=Snapshot(published);
        bool completed=false;
        for(size_t limit=0;limit<256;++limit)
        {
            ClearFailures();
            if(owner==0) FailSceneCatalogAllocationAfter(limit);
            else if(owner==1) json::FailAllocationAfter(limit);
            else FailFileAllocationAfter(limit);
            SettingsSnapshotError error;
            const std::string_view aliased[]{published[0].FileName,part};
            const bool success=BuildSceneCatalog(root.native(),aliased,published,error);
            ClearFailures();
            if(success)
            {
                Require(Snapshot(published)==before,"successful rebuild changed catalog values");
                completed=true;break;
            }
            if(owner==0)++catalogFailures;else if(owner==1)++jsonFailures;else ++fileFailures;
            Require(error.code==SettingsSnapshotErrorCode::OutOfMemory,"allocation exhaustion became metadata fallback");
            Require(published.Entries()==entries && published[0].DisplayName.data()==view.data() &&
                published[0].DisplayName==view && Snapshot(published)==before,"failed build replaced published owner or views");
            if(owner==2) Require(error.nativeCode==0,"file allocation failure invented a native code");
        }
        Require(completed,"catalog allocation enumeration did not reach success");
    }
    Require(catalogFailures>5 && jsonFailures>=3 && fileFailures==1,"allocation enumeration missed an owner");
    for(unsigned failure=0;failure<2;++failure)
    {
        SceneCatalog catalog;SettingsSnapshotError error;
        if(failure==0) SetFileReadTestLimits(1,0,false);else SetFileReadTestLimits(SIZE_MAX,SIZE_MAX,true);
        const bool success=BuildSceneCatalog(root.native(),discovered,catalog,error);ClearFailures();
        Require(success && catalog.Count()==2,"ordinary file failure stopped catalog fallback");
        const SceneCatalogEntry* entry=nullptr;
        Require(FindSceneCatalogEntry(catalog,file,entry,error) && entry && entry->DisplayName=="owner.scene.json" &&
            !entry->InitialCamera,"ordinary file failure published partial descriptor metadata");
    }
    SceneCatalog catalog;seed(catalog);
    SettingsSnapshotError error;
    AssignDetail(error,file);
    const std::string_view borrowed[]{error.MessageView(),part};
    Require(BuildSceneCatalog(root.native(),borrowed,catalog,error) && catalog.Count()==1,
        "build destroyed an input borrowed from the previous diagnostic");
    AssignDetail(error,file);
    const SceneCatalogEntry* entry=nullptr;
    Require(FindSceneCatalogEntry(catalog,error.MessageView(),entry,error) && entry==catalog.Entries(),
        "lookup destroyed an input borrowed from the previous diagnostic");
    AssignDetail(error,file);
    json::EncodedText display;
    Require(MakeSceneDisplayName(root.native(),error.MessageView(),display,error) &&
        std::string_view(display.Data(),display.Size())=="owner.scene.json","display formatting destroyed a diagnostic input");
    AssignDetail(error,file);const auto oldDetail=error.MessageView();
    Require(!MakeSceneDisplayName(root.native(),std::string_view(file),error.detail,error) &&
        error.MessageView().data()==oldDetail.data() && error.MessageView()==oldDetail,
        "display output alias replaced its error owner");

    size_t requestFailures=0;
    {
        const SceneCatalogEntry* matched=nullptr;
        Require(FindSceneCatalogEntry(catalog,file,matched,error) && matched,"could not locate request metadata");
        SceneLoadRequest request;
        Require(PrepareSceneLoadRequest(root.native(),file,matched,request,error),"could not prepare canonical request");
        Require(request.CatalogEntry()==matched && request.FileName().data()==matched->FileName.data() &&
            request.DisplayName().data()==matched->DisplayName.data() &&
            request.ImportFileName()==std::filesystem::path(matched->FileName).u8string(),"canonical request duplicated or changed catalog metadata");
        const auto externalPath=root/"sub"/".."/"external.glb";
        const std::string external=externalPath.generic_string(),imported=std::filesystem::path(external).u8string();
        Require(PrepareSceneLoadRequest(root.native(),external,nullptr,request,error),"could not prepare external request");
        Require(!request.CatalogEntry() && request.FileName()==external && request.DisplayName()=="external.glb" &&
            request.ImportFileName()==imported,"external request normalized identity or changed importer bytes");
        const auto oldFile=request.FileName(),oldDisplay=request.DisplayName(),oldImport=request.ImportFileName();
        bool complete=false;
        for(size_t limit=0;limit<256;++limit)
        {
            FailSceneCatalogAllocationAfter(limit);
            const bool success=PrepareSceneLoadRequest(root.native(),request.FileName(),nullptr,request,error);ClearFailures();
            if(success) { complete=true;break; }
            ++requestFailures;
            Require(error.code==SettingsSnapshotErrorCode::OutOfMemory && !request.CatalogEntry(),
                "failed request changed metadata or lost its allocation error");
            Require(request.FileName().data()==oldFile.data() && request.DisplayName().data()==oldDisplay.data() &&
                request.ImportFileName().data()==oldImport.data() && request.FileName()==external &&
                request.DisplayName()=="external.glb" && request.ImportFileName()==imported,
                "failed request replaced owned paths or their views");
        }
        Require(complete && requestFailures>5,"request allocation enumeration did not reach success");
        AssignDetail(error,external);
        Require(PrepareSceneLoadRequest(root.native(),error.MessageView(),nullptr,request,error) && request.FileName()==external,
            "request preparation destroyed a diagnostic input");
        const auto retainedFile=request.FileName();
        SceneLoadRequest movedRequest(std::move(request));
        Require(request.FileName().empty() && request.DisplayName().empty() && request.ImportFileName().empty() &&
            !request.CatalogEntry() && movedRequest.FileName().data()==retainedFile.data(),"request move lost ownership");
        movedRequest=std::move(movedRequest);
        Require(movedRequest.FileName().data()==retainedFile.data(),"request self move discarded text");
        request=std::move(movedRequest);
        Require(request.FileName().data()==retainedFile.data() && movedRequest.FileName().empty(),"request move assignment lost text");
        Require(setlocale(LC_ALL,".UTF8")!=nullptr,"could not select UTF8 request locale");
        Require(!PrepareSceneLoadRequest(root.native(),std::string_view("\xc3",1),nullptr,request,error) &&
            error.code==SettingsSnapshotErrorCode::Path && error.nativeCode==ERROR_NO_UNICODE_TRANSLATION &&
            request.FileName().data()==retainedFile.data() && request.FileName()==external,
            "invalid path conversion replaced the accepted request");
        Require(setlocale(LC_ALL,"C")!=nullptr,"could not restore request locale");
        request.Clear();request.Clear();
        Require(request.FileName().empty() && request.ImportFileName().empty() && !request.CatalogEntry(),"request Clear retained state");
    }

    const auto before=Snapshot(catalog);const auto* retained=catalog.Entries();
    entry=retained;FailSceneCatalogAllocationAfter(0);
    Require(!FindSceneCatalogEntry(catalog,file,entry,error) && entry==retained,"failed lookup changed the output entry");ClearFailures();
    const auto displayData=display.Data();const auto displaySize=display.Size();
    FailSceneCatalogAllocationAfter(0);
    Require(!MakeSceneDisplayName(root.native(),std::string_view(file),display,error) &&
        display.Data()==displayData && display.Size()==displaySize,"failed display formatting replaced published text");ClearFailures();
    json::FailAllocationAfter(0);
    Require(!MakeSceneDisplayName(root.native(),std::string_view(file),display,error) &&
        display.Data()==displayData && display.Size()==displaySize,"failed display text publication replaced output");ClearFailures();
    Require(!BuildSceneCatalog(root.native(),{nullptr,1},catalog,error) && catalog.Entries()==retained && Snapshot(catalog)==before,
        "invalid discovery range replaced the catalog");
    const std::string_view invalid[]{std::string_view(nullptr,1)};
    Require(!BuildSceneCatalog(root.native(),invalid,catalog,error) && catalog.Entries()==retained && Snapshot(catalog)==before,
        "invalid filename range replaced the catalog");
    SceneCatalog moved(std::move(catalog));
    Require(catalog.Count()==0 && !catalog.Entries() && moved.Entries()==retained && Snapshot(moved)==before,
        "catalog move lost storage or left a live moved-from owner");
    moved=std::move(moved);
    Require(moved.Entries()==retained && Snapshot(moved)==before,"self move discarded the catalog");
    seed(catalog);catalog=std::move(moved);
    Require(catalog.Entries()==retained && moved.Count()==0 && !moved.Entries() && Snapshot(catalog)==before,
        "move assignment lost the published arena");
    FailSceneCatalogAllocationAfter(0);
    Require(BuildSceneCatalog({}, {}, catalog,error) && catalog.Count()==0 && !catalog.Entries(),
        "empty catalog required storage or kept stale entries");ClearFailures();
    catalog.Clear();catalog.Clear();Require(catalog.Count()==0 && !catalog.Entries(),"repeated Clear was not empty");

    namespace cp=uvsr::catalog_path;
    Require(setlocale(LC_ALL,".UTF8")!=nullptr,"could not select UTF8 locale");SetFileApisToANSI();
    cp::Error pathError;cp::ConversionPlan plan;size_t size=0;
    Require(cp::MeasureDecode(std::string_view("\xc3\xa9",2),cp::Encoding::Filesystem,plan,pathError),"could not measure UTF8 path");
    wchar_t native[3]{L'x',L'x',L'x'};
    Require(setlocale(LC_ALL,"C")!=nullptr,"could not select C locale");SetFileApisToOEM();
    Require(cp::Decode(std::string_view("\xc3\xa9",2),plan,native,3,size,pathError) && size==1 && native[0]==0xe9,
        "decode changed the measured code page");
    Require(setlocale(LC_ALL,".UTF8")!=nullptr,"could not select UTF8 locale");
    Require(cp::MeasureEncode(std::wstring_view(native,1),plan,pathError),"could not measure narrow path");
    Require(setlocale(LC_ALL,"C")!=nullptr,"could not restore C locale");SetFileApisToANSI();char encoded[3]{'x','x','x'};
    Require(cp::Encode(std::wstring_view(native,1),plan,encoded,3,size,pathError) && size==2 &&
        static_cast<unsigned char>(encoded[0])==0xc3 && static_cast<unsigned char>(encoded[1])==0xa9,
        "encode changed the measured code page");
    const std::wstring_view components[]{L"a",L"b",L""};size_t cursor=0,index=0;std::wstring_view component;
    while(cp::Next(L"a//b///",cursor,component))
    {
        Require(index<3 && component==components[index],"path iteration retained a nonterminal empty component");++index;
    }
    Require(index==3,"path iteration lost the trailing empty component");
    printf("scene catalog ownership passed: %zu assertions, %zu catalog, %zu JSON, %zu file and %zu request allocation failures\n",
        Assertions,catalogFailures,jsonFailures,fileFailures,requestFailures);
}

#if defined(UVSR_SCENE_CATALOG_STANDALONE_PROBE)
int main(int argc,char** argv)
{
    try
    {
        Require(argc==2,"expected a private fixture root");
        TestSceneCatalogOwnership(std::filesystem::path(argv[1]));return 0;
    }
    catch(const std::exception& error)
    {
        ClearFailures();fprintf(stderr,"%s\n",error.what());return 1;
    }
}
#endif
