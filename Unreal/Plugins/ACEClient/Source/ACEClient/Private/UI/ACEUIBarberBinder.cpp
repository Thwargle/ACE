#include "UI/ACEUICharGenBinder.h"
#include "UI/ACEUIGameplayBinder.h"
#include "UI/ACEUICanvasWidget.h"
#include "UI/ACEUIElementManager.h"
#include "UI/ACEUILayoutResolver.h"
#include "UI/ACEUIResourceResolver.h"
#include "ACEClientSubsystem.h"
#include "ACEDatSubsystem.h"
#include "ACESession.h"
#include "ACEPlayerController.h"
#include "VR/ACEVRComponent.h"
#include "Misc/App.h"

bool UACEUICharGenBinder::InitializeBarber(UACEClientSubsystem* InClient,UACEUICanvasWidget* InCanvas,AACEPlayerController* InController)
{
    Client=InClient;Canvas=InCanvas;Controller=InController;Manager=Canvas?Canvas->GetManager():nullptr;
    const auto S=Client?Client->GetSession():nullptr;
    auto* Resources=Canvas?Canvas->GetResourceResolver():nullptr;
    auto* Dat=Resources?Resources->GetDatSubsystem():nullptr;FString Error;
    if(!S || !S->IsBarberOpen() || !Manager || !Dat || !Dat->GetPortalDat() || !Model.Load(*Dat->GetPortalDat(),Error))return false;
    Model.LoadStrings(Dat->GetDatDirectory());BarberStrings.LoadStrings(Dat->GetDatDirectory(),0x23000001);
    BarberOriginal=S->GetBarberProfile();
    const auto& V=S->GetPlayerVitals();
    if(!Model.RestoreBarber(BarberOriginal,V.HeritageGroup,V.Gender))return false;
    BarberInitial=Model.Selection;bSuppressEffect=BarberOriginal.HasSuppressedEffect(Model.Selection.Heritage);
    BarberRoot=Manager->FindElementByName(TEXT("RootGameplay_FloatyBarber_Field"));
    if(!BarberRoot)BarberRoot=Manager->FindElementByName(TEXT("BarberField"));
    if(!BarberRoot)BarberRoot=UACEUILayoutResolver::LoadTemplate(0x2100000F,0x10000598);
    auto Root=Manager->FindElementByName(TEXT("RootGameplay_Field"));
    if(!BarberRoot || !Root)return false;
    Root->AddChild(BarberRoot);BarberRoot->SetElementName(TEXT("RootGameplay_FloatyBarber_Field"));
    BarberRoot->bVisible=true;bBarber=true;Page=4;bClothes=false;bPreviewDirty=true;RotateDirection=0;PreviewYaw=180;Zoom=0;
    ActivatedHandle=Manager->OnElementActivated.AddUObject(this,&UACEUICharGenBinder::Activate);
    Manager->BringFloatyToFront(BarberRoot);
    return true;
}

void UACEUICharGenBinder::RefreshBarberOptions()
{
    // These labels belong to the general string table, while face choices use chargen strings.
    TFunction<void(TSharedPtr<FACEUIElement>)> LabelsFromTable=[&](TSharedPtr<FACEUIElement> E)
    {
        if(!E)return;
        if(const FString* Text=BarberStrings.Strings.Find(E->TextEntryId))Label(E,*Text);
        for(auto C:E->Children)LabelsFromTable(C);
    };
    LabelsFromTable(BarberRoot);
    Selected(TEXT("RotateClockwise"),RotateDirection==1);Selected(TEXT("RotateCounterClockwise"),RotateDirection==-1);
    const uint32 H=Model.Selection.Heritage;
    const bool HasOption=H==5 || H==9 || H==10 || H==11;
    Show(TEXT("BarberOption1"),HasOption);Show(TEXT("BarberOption2"),false);Show(TEXT("BarberOption3"),false);
    if(auto E=Find(TEXT("BarberOption1"))){E->bBooleanButton=true;E->bHighlighted=bSuppressEffect;}
    if(HasOption)Label(TEXT("BarberOption1"),BarberStrings.Text(H==9?TEXT("ID_Barber_Empyrean_Earthbound"):
        H==11?TEXT("ID_Barber_Undead_NoFlame"):TEXT("ID_Barber_Shadow_NoCrown")));
    for(const TCHAR* Name:{TEXT("BarberApplyButton"),TEXT("BarberCancelButton")})if(auto E=Find(Name))E->Y=HasOption?298:274;
    for(uint32 Id:{0x1000030au,0x1000030bu})if(auto Arrow=Child(Find(TEXT("SkinSpin")),Id))Arrow->bVisible=false;
}

void UACEUICharGenBinder::FinishBarber(bool Apply)
{
    const auto S=Client?Client->GetSession():nullptr;if(!S)return;
    if(Apply){if(!S->FinishBarber(Model.BuildBarberProfile(BarberOriginal,BarberInitial,bSuppressEffect)))return;}
    else S->CloseBarber();
    MouseUp();if(BarberRoot)BarberRoot->bVisible=false;
}

bool UACEUICharGenBinder::BuildBarberPreview(FACEWorldObject& Object) const
{
    if(!Model.BuildAppearance(Object,true))return false;
    using B=FACEBarberProfile;const auto P=Model.BuildBarberProfile(BarberOriginal,BarberInitial,bSuppressEffect);
    Object.SetupId=P.Values[B::Setup];Object.Appearance.PaletteBaseId=P.Values[B::BasePalette];
    if(P.Values[B::Head])
    {
        auto* Head=Object.Appearance.AnimPartChanges.FindByPredicate([](const auto& A){return A.PartIndex==16;});
        if(!Head)Head=&Object.Appearance.AnimPartChanges.AddDefaulted_GetRef();
        Head->PartIndex=16;Head->PartId=P.Values[B::Head];
    }
    for(int32 Field:{B::HairTexture,B::EyesTexture,B::NoseTexture,B::MouthTexture})
    {
        if(!P.Values[Field] || !P.Values[Field+1])continue;
        auto* T=Object.Appearance.TextureChanges.FindByPredicate([&](const auto& A){return uint32(A.OldTexture)==P.Values[Field+1];});
        if(!T){T=&Object.Appearance.TextureChanges.AddDefaulted_GetRef();T->PartIndex=16;}
        T->OldTexture=P.Values[Field+1];T->NewTexture=P.Values[Field];
    }
    const int32 Offsets[]={0,192,256},Counts[]={192,64,64};
    for(int32 I=0;I<3;++I)if(P.Values[B::SkinPalette+I])
    {
        Object.Appearance.SubPalettes.RemoveAll([&](const auto& A){return A.Offset==Offsets[I];});
        auto& Pal=Object.Appearance.SubPalettes.AddDefaulted_GetRef();Pal.SubPaletteId=P.Values[B::SkinPalette+I];Pal.Offset=Offsets[I];Pal.NumColors=Counts[I];
    }
    return true;
}

void UACEUIGameplayBinder::RefreshBarber()
{
    const auto S=Client?Client->GetSession():nullptr;
    if(!S || S->GetState()!=EACESessionState::InWorld || !S->IsBarberOpen())
    {
        if(Barber){Barber->Shutdown();Barber=nullptr;}
        if(Canvas)Canvas->SetBarberBinder(nullptr);
        return;
    }
    if(!Barber || SeenBarberRevision!=S->GetBarberRevision())
    {
        if(Barber)Barber->Shutdown();
        Barber=NewObject<UACEUICharGenBinder>(this);SeenBarberRevision=S->GetBarberRevision();
        if(!Barber->InitializeBarber(Client,Canvas,PlayerController))
        {
            Barber->Shutdown();Barber=nullptr;S->CloseBarber();if(Canvas)Canvas->SetBarberBinder(nullptr);
            PostInventorySystemMessage(TEXT("Unable to load barber appearance data. Check your game DAT files."));return;
        }
        Canvas->SetBarberBinder(Barber);
        if(PlayerController && PlayerController->GetPawn())
            if(auto* VR=PlayerController->GetPawn()->FindComponentByClass<UACEVRComponent>();VR && VR->IsActive())VR->OpenRetailPanel(NAME_None);
    }
    Barber->Tick(FApp::GetDeltaTime());
}
