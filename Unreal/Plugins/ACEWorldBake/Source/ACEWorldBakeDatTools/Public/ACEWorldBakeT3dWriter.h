#pragma once

struct FACEWorldBakePortalModel;

struct ACEWORLDBAKEDATTOOLS_API FACEWorldBakeT3dContent
{
	FACEWorldBakeT3dContent(int32 InDepth);
	FACEWorldBakeT3dContent(const FString& InContent, int32 InDepth);
	virtual ~FACEWorldBakeT3dContent() {}

	virtual operator FString() const { return Content; }

	const FString& GetContent() const { return Content; }

protected:
	FString Content;
	FString Indent;
	const int32 TabDepth;
	const int32 PrevDepth;
	const int32 NextDepth;
};

struct ACEWORLDBAKEDATTOOLS_API FACEWorldBakeT3dSection : public FACEWorldBakeT3dContent
{
	FACEWorldBakeT3dSection(const FString& InSectionTag, const FString& InSectionValue, int32 InDepth);
	virtual ~FACEWorldBakeT3dSection() {}

	virtual operator FString() const override;

	virtual void AppendBody(FString& OutContent) const {}

	void AppendFooter(FString& OutContent) const;

	FString SectionTag;
};

struct ACEWORLDBAKEDATTOOLS_API FACEWorldBakeT3dWriter
{
	static FString ExportText(const int32 CellX, const int32 CellY, const struct FACEWorldBakeCellLandblockObject& LandblockInfos);

	static FString ExportLandblockScenesText(const int32 StartX, const int32 StartY, const int32 CountX, const int32 CountY);

	static FString ExportText(const struct FACEWorldBakePortalScene& Scene);

	static FString ExportText(const struct FACEWorldBakePortalSetup& Setup);
};
