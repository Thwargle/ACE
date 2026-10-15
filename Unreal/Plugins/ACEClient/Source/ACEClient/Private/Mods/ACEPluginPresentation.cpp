#include "Mods/ACEPluginPresentation.h"
#include "Mods/ACEPluginIcon.h"
#include "Mods/ACEPluginSubsystem.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "ImageUtils.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"

namespace
{
    FString Value(const FACEClientPlugin& P,const TCHAR* Key)
    {
        FString Text;
        if(P.Presentation&&P.Presentation->TryGetStringField(Key,Text))return Text;
        const TSharedPtr<FJsonObject>* Defaults=nullptr;
        if(P.Manifest&&P.Manifest->TryGetObjectField(TEXT("presentation"),Defaults))(*Defaults)->TryGetStringField(Key,Text);
        return Text;
    }
}
FString FACEClientPlugin::DisplayTitle() const
{const FString Title=Value(*this,TEXT("title"));return Title.IsEmpty()?Name:Title;}
FString FACEClientPlugin::DisplayIcon() const {return Value(*this,TEXT("icon"));}

bool ACEPluginPresentation::ValidAsset(const FString& Asset)
{
    return !Asset.IsEmpty()&&Asset.Len()<=128&&Asset.EndsWith(TEXT(".png"))
        &&Asset==FPaths::GetCleanFilename(Asset)&&!Asset.Contains(TEXT(":"))
        &&!Asset.Contains(TEXT("/"))&&!Asset.Contains(TEXT("\\"))&&!Asset.Contains(TEXT(".."));
}
bool ACEPluginPresentation::Valid(const TSharedPtr<FJsonObject>& O,FString& Error)
{
    if(!O){Error=TEXT("presentation must be an object");return false;}
    for(const auto& Pair:O->Values)
    {
        if(Pair.Key==TEXT("window"))
        {
            const TSharedPtr<FJsonObject>* Window=nullptr;
            if(!Pair.Value->TryGetObject(Window)){Error=TEXT("presentation.window must be an object");return false;}
            for(const auto& Field:(*Window)->Values)
            {
                double Opacity=0;bool Pinned=false;
                if(Field.Key==TEXT("opacity")&&Field.Value->Type==EJson::Number&&Field.Value->TryGetNumber(Opacity)&&FMath::IsFinite(Opacity)&&Opacity>=0&&Opacity<=1)continue;
                if(Field.Key==TEXT("pinned")&&Field.Value->Type==EJson::Boolean&&Field.Value->TryGetBool(Pinned))continue;
                Error=TEXT("presentation.window accepts opacity (0..1) and pinned (boolean)");return false;
            }
            continue;
        }
        FString Text;
        if((Pair.Key!=TEXT("title")&&Pair.Key!=TEXT("icon"))||!Pair.Value->TryGetString(Text))
        {Error=TEXT("presentation accepts title/icon strings and window appearance");return false;}
        if(Pair.Key==TEXT("title"))
        {
            if(Text.Len()>80){Error=TEXT("Plugin title is limited to 80 characters");return false;}
            for(TCHAR Ch:Text)if(FChar::IsControl(Ch)){Error=TEXT("Plugin title must be a single line");return false;}
        }
        else if(!Text.IsEmpty()&&(Text.Len()>96||!ValidAsset(Text))){Error=TEXT("Plugin icon must be a PNG filename in the plugin's images folder");return false;}
    }
    return true;
}
UTexture2D* FACEPluginImageCache::Load(const FString& Directory,const FString& Asset)
{
    if(const auto* Cached=Images.Find(Asset))return Cached->Get();
    if(Images.Num()>=32||!ACEPluginPresentation::ValidAsset(Asset))return nullptr;
    // Remember missing/bad assets too; Lua refreshes must not repeatedly hit disk.
    Images.Add(Asset,TStrongObjectPtr<UTexture2D>());
    const FString Path=Directory/TEXT("images")/Asset;const int64 Size=IFileManager::Get().FileSize(*Path);
    if(Size<=0||Size>2*1024*1024)return nullptr;TArray<uint8> Bytes;if(!FFileHelper::LoadFileToArray(Bytes,*Path))return nullptr;
    auto& Module=FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));auto Decoder=Module.CreateImageWrapper(EImageFormat::PNG);
    if(!Decoder||!Decoder->SetCompressed(Bytes.GetData(),Bytes.Num())||Decoder->GetWidth()>1024||Decoder->GetHeight()>1024||Decoder->GetWidth()<1||Decoder->GetHeight()<1)return nullptr;
    const int64 Decoded=int64(Decoder->GetWidth())*Decoder->GetHeight()*4;if(ImageBytes+Decoded>16*1024*1024)return nullptr;
    auto* Texture=FImageUtils::ImportBufferAsTexture2D(Bytes);if(!Texture)return nullptr;
    ImageBytes+=Decoded;Images.Add(Asset,TStrongObjectPtr<UTexture2D>(Texture));return Texture;
}
void SACEPluginBadge::Construct(const FArguments& A){Plugin=A._Plugin;Refresh();}
void SACEPluginBadge::Tick(const FGeometry& G,double T,float D){SCompoundWidget::Tick(G,T,D);Refresh();}
void SACEPluginBadge::Refresh()
{
    if(!Plugin)return;
    const FString Next=Plugin->DisplayIcon();if(Initialized&&Next==Asset)return;Asset=Next;Initialized=true;
    if(!Plugin->Images)Plugin->Images=MakeShared<FACEPluginImageCache>();
    if(auto* Texture=Plugin->Images->Load(Plugin->Directory,Asset))
    {
        Brush.SetResourceObject(Texture);Brush.ImageSize=FVector2D(Texture->GetSizeX(),Texture->GetSizeY());
        ChildSlot[SNew(SBox).WidthOverride(26).HeightOverride(26)
            [SNew(SScaleBox).Stretch(EStretch::ScaleToFit)[SNew(SImage).Image(&Brush)]]];
    }
    else ChildSlot[SNew(SBox).WidthOverride(26).HeightOverride(26)[SNew(SACEPluginIcon).Id(Plugin->Id)]];
}
