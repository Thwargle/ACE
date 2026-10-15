#pragma once

#include "CoreMinimal.h"
#include "Input/DragAndDrop.h"
#include "Widgets/SCompoundWidget.h"

class UACEPluginSubsystem;

/** Only plugin ordering drags are accepted; inventory payloads cannot reorder UI. */
class FACEPluginOrderDrag : public FDragDropOperation
{
public:
    DRAG_DROP_OPERATOR_TYPE(FACEPluginOrderDrag,FDragDropOperation)
    static TSharedRef<FACEPluginOrderDrag> New(UACEPluginSubsystem* Host,const FString& Id);
    TWeakObjectPtr<UACEPluginSubsystem> Host;
    FString Id;
    virtual TSharedPtr<SWidget> GetDefaultDecorator() const override;
private:
    FString Title;
};

namespace ACEPluginOrder
{
    FReply BeginDrag(UACEPluginSubsystem* Host,const FString& Id,const FString& LockId);
}

/** Drop anywhere on a manager row or bar icon; the insertion line shows the side. */
class SACEPluginOrderTarget : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SACEPluginOrderTarget):_Host(nullptr),_Horizontal(false){}
        SLATE_ARGUMENT(UACEPluginSubsystem*,Host)
        SLATE_ARGUMENT(FString,Id)
        SLATE_ARGUMENT(FString,LockId)
        SLATE_ATTRIBUTE(bool,Horizontal)
        SLATE_DEFAULT_SLOT(FArguments,Content)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args);
    virtual FReply OnDragOver(const FGeometry& Geometry,const FDragDropEvent& Event) override;
    virtual void OnDragLeave(const FDragDropEvent& Event) override;
    virtual FReply OnDrop(const FGeometry& Geometry,const FDragDropEvent& Event) override;
    virtual int32 OnPaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& Culling,
        FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool Enabled) const override;
private:
    TWeakObjectPtr<UACEPluginSubsystem> Host;
    FString Id,LockId;
    TAttribute<bool> Horizontal;
    bool Hovered=false,After=false;
    bool CanAccept(const FDragDropEvent& Event) const;
    bool IsAfter(const FGeometry& Geometry,const FDragDropEvent& Event) const;
};
