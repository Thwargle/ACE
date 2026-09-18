#pragma once

struct FACEWorldBakePortalModel;
struct FACEWorldBakePortalEnvironment;

struct ACEWORLDBAKEDATTOOLS_API FACEWorldBakeFbxContent
{
	FACEWorldBakeFbxContent(int32 InDepth);
	FACEWorldBakeFbxContent(const FString& InContent, int32 InDepth);
	virtual ~FACEWorldBakeFbxContent() {}

	virtual operator FString() const { return Content; }

	const FString& GetContent() const { return Content; }

protected:
	FString Content;
	FString Indent;
	const int32 Depth;
	const int32 PrevDepth;
	const int32 NextDepth;
};

struct ACEWORLDBAKEDATTOOLS_API FACEWorldBakeFbxValue : public FACEWorldBakeFbxContent
{
	FACEWorldBakeFbxValue(const FString& InKey, const FString& InValue, int32 InDepth);
	FACEWorldBakeFbxValue(const FString& InKey, const int32& InValue, int32 InDepth);
	FACEWorldBakeFbxValue(const FString& InKey, const float& InValue, int32 InDepth);
	FACEWorldBakeFbxValue(const FString& InKey, const TArray<FString>& InValue, int32 InDepth);
	FACEWorldBakeFbxValue(const FString& InKey, const TArray<int32>& InValue, int32 InDepth);
	FACEWorldBakeFbxValue(const FString& InKey, const TArray<float>& InValue, int32 InDepth);
	virtual ~FACEWorldBakeFbxValue() {}
};

struct ACEWORLDBAKEDATTOOLS_API FACEWorldBakeFbxSection : public FACEWorldBakeFbxContent
{
	FACEWorldBakeFbxSection(const FString& InSectionTag, int32 InDepth);
	FACEWorldBakeFbxSection(const FString& InSectionTag, const FString& InSectionName, int32 InDepth);
	FACEWorldBakeFbxSection(const FString& InSectionTag, const int32& InSectionId, int32 InDepth);

	virtual ~FACEWorldBakeFbxSection() {}

	virtual operator FString() const override;

	virtual void AppendBody(FString& OutContent) const {}

	void AppendFooter(FString& OutContent) const;
};

struct ACEWORLDBAKEDATTOOLS_API FACEWorldBakeFbxWriter
{
	static FString ExportText(const FACEWorldBakePortalEnvironment& Environment);

	static FString ExportPortalsText(const FACEWorldBakePortalEnvironment& Environment);

	static FString ExportText(const FACEWorldBakePortalModel& Model);
};
