#include "Sky/ACELegionsEnvironment.h"
#include "TextureResource.h"

UTexture2D* ACELegionsSky::CreateImportedTexture(const FImage& Image, TextureFilter Filter)
{
	auto* Tex = UTexture2D::CreateTransient(Image.Width, Image.Height, Image.Format);
	if (!Tex)
		return nullptr;
	Tex->SRGB = true;
	Tex->AddressX = TA_Wrap;
	Tex->AddressY = TA_Wrap;
	Tex->Filter = Filter;
	Tex->NeverStream = true;
	auto& Mips = Tex->GetPlatformData()->Mips;
	for (int32 I = 0; I < Image.Mips.Num(); ++I)
	{
		if (I > 0)
			Mips.Add(new FTexture2DMipMap());
		auto& Mip = Mips[I];
		Mip.SizeX = FMath::Max(1, Image.Width >> I);
		Mip.SizeY = FMath::Max(1, Image.Height >> I);
		const auto& Pixels = Image.Mips[I];
		const int64 Bytes = Pixels.Num();
		Mip.BulkData.Lock(LOCK_READ_WRITE);
		FMemory::Memcpy(Mip.BulkData.Realloc(Bytes), Pixels.GetData(), Bytes);
		Mip.BulkData.Unlock();
	}
	Tex->UpdateResource();
	return Tex;
}
