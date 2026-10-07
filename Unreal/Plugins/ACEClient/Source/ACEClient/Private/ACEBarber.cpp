#include "ACECharacterCreation.h"
#include "ACESession.h"
#include "Dat/ACEDatDatabase.h"
#include "Protocol/ACEBinaryReader.h"

uint32 FACEBarberProfile::EffectSetup(uint32 Heritage,uint32 Gender,uint32 SetupId,bool Suppressed)
{
	// gmBarberUI / ACE SetupConst: these variants are defined in the client, not chargen DAT.
	const bool Female=Gender==2;
	if(Heritage==5) return Suppressed?(Female?0x02001A5Eu:0x02001A5Fu):(Female?0x02001970u:0x0200196Fu);
	if(Heritage==10) return Suppressed?(Female?0x02001A5Cu:0x02001A5Du):(Female?0x0200196Du:0x0200196Eu);
	if(Heritage==11)
	{
		const bool Zombie=SetupId==0x02001AA1 || SetupId==0x02001AA2 || SetupId==0x02001A9D || SetupId==0x02001A96;
		if(Female) return Zombie?(Suppressed?0x02001AA2u:0x02001AA1u):(Suppressed?0x02001A9Fu:0x02001AA0u);
		return Zombie?(Suppressed?0x02001A96u:0x02001A9Du):(Suppressed?0x02001A9Eu:0x02001A9Cu);
	}
	return SetupId;
}
bool FACEBarberProfile::HasSuppressedEffect(uint32 Heritage) const
{
	if(Heritage==9) return Values[Option1]!=0;
	if(Heritage!=5 && Heritage!=10 && Heritage!=11) return false;
	return Values[Setup]==EffectSetup(Heritage,1,Values[Setup],true)
		|| Values[Setup]==EffectSetup(Heritage,2,Values[Setup],true);
}

bool FACECharacterCreation::RestoreBarber(const FACEBarberProfile& P,uint32 HeritageId,uint32 Gender)
{
	if(!SelectHeritage(HeritageId) || !SelectSex(Gender)) return false;
	const auto* S=Sex(); if(!S) return false;
	for(auto& Gear:Selection.GearStyle) Gear=MAX_uint32;
	using B=FACEBarberProfile;
	for(int32 I=0;I<S->Hair.Num();++I)
		if(S->Hair[I].Desc.AnimPartChanges.ContainsByPredicate([&](const auto& A){return uint32(A.PartId)==P.Values[B::Head];})
			&& (P.Values[B::HairTexture]==0 || S->Hair[I].Desc.TextureChanges.ContainsByPredicate([&](const auto& T){return uint32(T.NewTexture)==P.Values[B::HairTexture];})))
		{Selection.HairStyle=I;break;}
	const bool Bald=S->Hair.IsValidIndex(Selection.HairStyle) && S->Hair[Selection.HairStyle].Bald;
	auto Match=[&](const TArray<FACECGFace>& Faces,uint32 Texture,uint32& Index,bool Eyes=false)
	{
		for(int32 I=0;I<Faces.Num();++I)
		{
			const auto& Desc=Eyes && Bald?Faces[I].BaldDesc:Faces[I].Desc;
			if(Desc.TextureChanges.ContainsByPredicate([&](const auto& T){return uint32(T.NewTexture)==Texture;})) {Index=I;return;}
		}
	};
	Match(S->Eyes,P.Values[B::EyesTexture],Selection.Eyes,true);
	Match(S->Nose,P.Values[B::NoseTexture],Selection.Nose);
	Match(S->Mouth,P.Values[B::MouthTexture],Selection.Mouth);
	auto Shade=[&](uint32 Set,uint32 PaletteId,double& Hue)
	{
		TArray<uint8> Bytes;if(!Database || !Database->ReadFile(Set,Bytes))return false;
		FACEBinaryReader R(Bytes);if(!R.CanRead(8))return false;R.ReadUInt32();const uint32 Count=R.ReadUInt32();
		if(Count>uint32(R.Remaining()/4))return false;
		for(uint32 I=0;I<Count;++I)if(R.ReadUInt32()==PaletteId){Hue=(double(I)+.5)/Count;return true;}
		return false;
	};
	Shade(S->SkinPalSet,P.Values[B::SkinPalette],Selection.SkinHue);
	for(int32 I=0;I<S->HairColors.Num();++I)
		if(Shade(S->HairColors[I],P.Values[B::HairPalette],Selection.HairHue)){Selection.HairColor=I;break;}
	const int32 Eye=S->EyeColors.IndexOfByKey(P.Values[B::EyesPalette]);if(Eye!=INDEX_NONE)Selection.EyeColor=Eye;
	return true;
}

FACEBarberProfile FACECharacterCreation::MakeBarberProfile() const
{
	FACEBarberProfile P;using B=FACEBarberProfile;const auto* S=Sex();if(!S)return P;
	P.Values[B::BasePalette]=S->BasePalette;P.Values[B::Setup]=S->Setup;
	bool AlternateFace=false;
	auto Texture=[&](const FACEObjDesc& Desc,int32 Field)
	{if(!Desc.TextureChanges.IsEmpty()){const auto& T=Desc.TextureChanges[AlternateFace && Desc.TextureChanges.Num()>1?1:0];P.Values[Field]=T.NewTexture;P.Values[Field+1]=T.OldTexture;}};
	bool Bald=false;
	if(S->Hair.IsValidIndex(Selection.HairStyle))
	{
		const auto& H=S->Hair[Selection.HairStyle];Bald=H.Bald;
		if(Selection.Heritage!=6 && !H.Desc.AnimPartChanges.IsEmpty())P.Values[B::Head]=H.Desc.AnimPartChanges[0].PartId;
		if(Selection.Heritage!=6 && Selection.Heritage!=12 && Selection.Heritage!=13)Texture(H.Desc,B::HairTexture);
		if(H.AlternateSetup)P.Values[B::Setup]=H.AlternateSetup;
		AlternateFace=H.AlternateSetup==0x02001A9C || H.AlternateSetup==0x02001AA0;
	}
	if(S->Eyes.IsValidIndex(Selection.Eyes))Texture(Bald?S->Eyes[Selection.Eyes].BaldDesc:S->Eyes[Selection.Eyes].Desc,B::EyesTexture);
	if(S->Nose.IsValidIndex(Selection.Nose))Texture(S->Nose[Selection.Nose].Desc,B::NoseTexture);
	if(S->Mouth.IsValidIndex(Selection.Mouth))Texture(S->Mouth[Selection.Mouth].Desc,B::MouthTexture);
	P.Values[B::SkinPalette]=Palette(S->SkinPalSet,Selection.SkinHue);
	if(S->HairColors.IsValidIndex(Selection.HairColor))P.Values[B::HairPalette]=Palette(S->HairColors[Selection.HairColor],Selection.HairHue);
	if(S->EyeColors.IsValidIndex(Selection.EyeColor))P.Values[B::EyesPalette]=S->EyeColors[Selection.EyeColor];
	return P;
}

FACEBarberProfile FACECharacterCreation::BuildBarberProfile(const FACEBarberProfile& Original,const FACECGSelection& Initial,bool SuppressEffect) const
{
	using B=FACEBarberProfile;FACEBarberProfile P=Original,New=MakeBarberProfile();
	auto Copy=[&](int32 First,int32 Last){for(int32 I=First;I<=Last;++I)P.Values[I]=New.Values[I];};
	const bool HairChanged=Initial.HairStyle!=Selection.HairStyle;
	if(HairChanged){Copy(B::Head,B::DefaultHairTexture);Copy(B::Setup,B::Setup);}
	if(HairChanged || Initial.Eyes!=Selection.Eyes)Copy(B::EyesTexture,B::DefaultEyesTexture);
	if(HairChanged || Initial.Nose!=Selection.Nose)Copy(B::NoseTexture,B::DefaultNoseTexture);
	if(HairChanged || Initial.Mouth!=Selection.Mouth)Copy(B::MouthTexture,B::DefaultMouthTexture);
	if(Initial.SkinHue!=Selection.SkinHue)Copy(B::SkinPalette,B::SkinPalette);
	if(Initial.HairColor!=Selection.HairColor || Initial.HairHue!=Selection.HairHue)Copy(B::HairPalette,B::HairPalette);
	if(Initial.EyeColor!=Selection.EyeColor)Copy(B::EyesPalette,B::EyesPalette);
	if(Selection.Heritage==9)P.Values[B::Option1]=SuppressEffect?1:0;
	if(HairChanged || SuppressEffect!=Original.HasSuppressedEffect(Selection.Heritage))
		P.Values[B::Setup]=B::EffectSetup(Selection.Heritage,Selection.Sex,P.Values[B::Setup],SuppressEffect);
	return P;
}

bool FACESession::FinishBarber(const FACEBarberProfile& Profile)
{
	if(State!=EACESessionState::InWorld || !bBarberOpen)return false;
	FACEBinaryWriter W;for(uint32 Value:Profile.Values)W.WriteUInt32(Value);
	SendGameAction(0x0311,W.GetData(),ACEQueue::WeenieQueue);
	bBarberOpen=false;return true;
}
