#include "VR/ACEVRComponent.h"
#include "ACEVRUIStyle.h"
#include "ACEEnemyHealthBar.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "ACEPlayerController.h"
#include "ACEWorldEntityActor.h"
#include "ACECharacterAppearanceComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/WidgetComponent.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/SlateRenderer.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"
#include "UI/ACEUICanvasWidget.h"
#include "UI/ACEUIElementManager.h"

void UACEVRComponent::NPCSpeech(int32 Guid, const FString& Text)
{
	ClearConversationSelection(Guid);
	ShowWorldNotice(Text, Guid, 0, FLinearColor(1.f,.9f,.65f));
}

void UACEVRComponent::ClearConversationSelection(int32 Guid)
{
	FACEWorldObject Object;
	if (!bActive || !Client || !Client->GetWorldObject(Guid,Object) || Object.bIsPlayer
		|| Object.IsCorpse() || !Object.IsGiveOrCreatureTarget()) return;
	ConversationHoverGuid = Guid;
	if (Client->GetSelectedObject().Guid == Guid) Client->SelectObject(0);
}

void UACEVRComponent::CombatFeedback(const FString& Name, int32 Amount, bool Incoming, bool Critical)
{
    if (Amount > 0 && Client && Client->GetSession() && Client->GetSession()->SupportsHealthFeedback()) return;
	int32 Guid = Incoming ? 0 : -1;
	if (!Incoming)
	{
		float Nearest = MAX_flt;
		for (TActorIterator<AACEWorldEntityActor> It(GetWorld()); It; ++It)
		{
			FACEWorldObject Object;
			if (!Client->GetWorldObject(It->GetACEGuid(), Object) || !Object.Name.Equals(Name, ESearchCase::IgnoreCase)) continue;
			const float Distance = FVector::DistSquared(It->GetActorLocation(), Head->GetComponentLocation());
			if (Distance < Nearest) { Guid = Object.Guid; Nearest = Distance; }
		}
	}
	const FString Text = Amount > 0 ? FString::Printf(TEXT("%s%d DAMAGE"),Critical ? TEXT("Crit! ") : TEXT(""),Amount) : Incoming ? TEXT("EVADED") : TEXT("MISSED");
	ShowWorldNotice(Text, Guid, 2, Amount == 0 ? FLinearColor(.824f,.824f,.784f) : Incoming ? FLinearColor(1.f,.247f,.247f) : FLinearColor(1.f,.8f,.25f));
}

void UACEVRComponent::VitalsFeedback(const FACEPlayerVitals& Vitals)
{
	if (!Client || !Vitals.bValid) return;
	const int32 Player = Client->GetPlayerGuid();
	const int32 Difference = Vitals.Health - NoticeHealth;
	if (!(Client->GetSession() && Client->GetSession()->SupportsHealthFeedback()) && Player && Player == NoticePlayerGuid && NoticeHealth >= 0 && Difference && !PC->bEnterWorldLoading && !PC->bWorldRevealActive)
		ShowWorldNotice(FString::Printf(TEXT("%s%d %s"), Difference>0 ? TEXT("+") : TEXT(""), FMath::Abs(Difference), Difference>0 ? TEXT("HEALTH") : TEXT("DAMAGE")), 0, 2, Difference > 0 ? FLinearColor(.5f,1.f,.498f) : FLinearColor(1.f,.247f,.247f));
	NoticePlayerGuid = Player; NoticeHealth = Vitals.Health;
}


void UACEVRComponent::HealthFeedback(int32 Guid, int32 Change, uint32 Flags)
{
    if (!Client || !Change || !PC || PC->bEnterWorldLoading || PC->bWorldRevealActive) return;
    const bool Self = Guid == Client->GetPlayerGuid();
    // Color describes the outcome, never the damage school. The YOU/target
    // heading and separate lanes make the recipient explicit without color.
    const FLinearColor Color = Change > 0 ? FLinearColor(.5f,1.f,.498f)
        : Self ? FLinearColor(1.f,.247f,.247f) : FLinearColor(1.f,.8f,.25f);
    const FString Text = FString::Printf(TEXT("%s%d %s"), Change>0 ? TEXT("+") : (Flags & 2u) ? TEXT("Crit! ") : TEXT(""),
        FMath::Abs(Change), Change>0 ? TEXT("HEALTH") : TEXT("DAMAGE"));
    ShowWorldNotice(Text, Self ? 0 : Guid, 2, Color);
}

namespace
{
constexpr float NoticeScrollSpeed = 12.f; // Slate pixels per second, about 2.5 seconds per line.
FVector NoticeActorPosition(AACEWorldEntityActor* Actor)
{
	FBox Visual(ForceInit);
	if (const auto* Appearance = Actor->FindComponentByClass<UACECharacterAppearanceComponent>();
		Appearance && Appearance->GetVisualWorldBounds(Visual))
		return FVector(Visual.GetCenter().X, Visual.GetCenter().Y, Visual.Max.Z);
	const auto* Capsule = Actor->FindComponentByClass<UCapsuleComponent>();
	return Capsule ? Capsule->Bounds.Origin + FVector(0,0,Capsule->Bounds.BoxExtent.Z)
		: Actor->GetActorLocation() + FVector(0,0,180);
}
}

void UACEVRComponent::ShowWorldNotice(const FString& Text, int32 Guid, int32 Kind, FLinearColor Color)
{
	if (!bActive || !Client || Client->GetSessionState() != EACESessionState::InWorld || !PresentationActor || Text.IsEmpty()) return;
	const double Now = FPlatformTime::Seconds();
	AACEWorldEntityActor* Anchor = nullptr;
	// Item-give chat includes the giver's name. Keep its receipt beside the same
	// NPC as the dialogue, including when the item message arrives first.
	const int32 GivesAt = Kind == 1 ? Text.Find(TEXT(" gives you ")) : INDEX_NONE;
	const FString Giver = GivesAt > 0 ? Text.Left(GivesAt) : FString();
	float Nearest = MAX_flt;
	for (TActorIterator<AACEWorldEntityActor> It(GetWorld()); (Guid || !Giver.IsEmpty()) && It; ++It)
	{
		if (Guid && It->GetACEGuid() == Guid) { Anchor = *It; break; }
		FACEWorldObject Object;
		if (!Guid && !Giver.IsEmpty() && Client->GetWorldObject(It->GetACEGuid(), Object) && Object.Name.Equals(Giver, ESearchCase::IgnoreCase))
		{
			const float Distance = FVector::DistSquared(It->GetActorLocation(), Head->GetComponentLocation());
			if (Distance < Nearest) { Anchor = *It; Nearest = Distance; }
		}
	}
	if (Kind == 0 && !Anchor) return;
	FWorldNotice* Notice = nullptr;
	if (Kind != 2) Notice = WorldNotices.FindByPredicate([&](const FWorldNotice& N) { return N.Kind == Kind && N.Actor == Anchor && N.Expires > Now; });
	const bool Append = Notice != nullptr && Kind < 2;
	const float PreviousOffset = Append && Notice->Scroll ? Notice->Scroll->GetScrollOffset() : 0.f;
    // Bound each recipient lane independently; an AOE cannot erase incoming hits.
    if (Kind==2)
    {
        int32 Count=0; FWorldNotice* Oldest=nullptr;
        for (auto& N:WorldNotices) if (N.Kind==2 && N.Expires>Now && (N.NumberLane<0)==(Guid==0))
        { ++Count; if (!Oldest || N.Started<Oldest->Started) Oldest=&N; }
        if (Count>=3) Notice=Oldest;
    }
	if (!Notice) Notice = WorldNotices.FindByPredicate([&](const FWorldNotice& N) { return N.Expires <= Now; });
	if (!Notice && WorldNotices.Num() < 12) Notice = &WorldNotices.AddDefaulted_GetRef();
	if (!Notice)
	{
		// Combat bursts must never erase a conversation the player is reading.
		for (auto& N : WorldNotices)
			if ((N.Kind == 2 || Kind != 2) && (!Notice || (N.Kind == 2 && Notice->Kind != 2) ||
				(N.Kind == Notice->Kind && N.Expires < Notice->Expires))) Notice = &N;
		if (!Notice) return;
	}
	if (!Notice->Panel.IsValid())
	{
		auto* Panel = NewObject<UWidgetComponent>(PresentationActor);
		PresentationActor->AddInstanceComponent(Panel);
		Panel->SetupAttachment(PresentationActor->GetRootComponent());
		Panel->SetWidgetSpace(EWidgetSpace::World); Panel->SetTwoSided(true);
		Panel->SetBlendMode(EWidgetBlendMode::Transparent);
		Panel->SetCollisionEnabled(ECollisionEnabled::NoCollision); Panel->SetCastShadow(false);
		Panel->SetTickWhenOffscreen(true);
		Panel->SetManuallyRedraw(true); Panel->RegisterComponent();
		Notice->Panel = Panel;
	}
	Notice->Text = Append ? Notice->Text + TEXT("\n\n") + Text : Text;
	// Bound transient UI memory without discarding the beginning of a speech.
	// The complete original messages remain in regular chat.
	if (Notice->Text.Len() > 65536) Notice->Text = Notice->Text.Left(65536) + TEXT("\nFurther dialogue is available in chat.");
	Notice->Color = Color; Notice->Actor = Anchor; Notice->Kind = Kind; Notice->Started = Now;
	Notice->Position = Anchor ? NoticeActorPosition(Anchor)
		: Head->GetComponentTransform().TransformPosition(FVector(110, Kind == 2 ? 28 : 0, Kind == 2 ? -12 : -38));
	Notice->Scroll.Reset();
	Notice->LastRedraw = Now;
	auto* Panel = Notice->Panel.Get();
	Panel->SetWorldScale3D(FVector(.1f));
	Panel->SetTranslucentSortPriority(Kind == 3 ? 120 : Kind == 2 ? 70 : Kind == 0 ? 32 : 31);
	if (Kind == 2)
	{
		Notice->Expires = Now + 2.8;
		Notice->NumberLane = Guid==0 ? -1.f : 1.f;
		FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Bold", 38);
		Font.OutlineSettings.OutlineSize = 3;
		Font.OutlineSettings.OutlineColor = FLinearColor::Black;
		Panel->SetDrawSize(FVector2D(620,124));
        FSlateFontInfo Heading = Font; Heading.Size = 26; Heading.OutlineSettings.OutlineSize = 1;
        FACEWorldObject Recipient;
		const bool Healing=Text.Contains(TEXT("HEALTH"));
		FString Title=Guid==0 ? (Healing?TEXT("YOU ARE HEALED"):Text==TEXT("EVADED")?TEXT("YOU EVADED"):TEXT("DAMAGE TAKEN"))
			: (Healing?TEXT("HEAL: "):Text==TEXT("MISSED")?TEXT("MISSED: "):TEXT("HIT: "))+(Client->GetWorldObject(Guid,Recipient)?Recipient.Name:TEXT("Enemy"));
        if (Title.Len()>32) Title=Title.Left(29)+TEXT("...");
		static const FSlateRoundedBoxBrush CombatBackground(FLinearColor(.012f,.016f,.024f,.82f),12.f);
		Panel->SetSlateWidget(SNew(SBorder).Visibility(EVisibility::HitTestInvisible)
			.BorderImage(&CombatBackground).Padding(8.f).HAlign(HAlign_Center).VAlign(VAlign_Center)
			[SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
                    [SNew(STextBlock).Text(FText::FromString(Title)).Font(Heading).ColorAndOpacity(ACEVRUIStyle::TextColor)
                        .ShadowOffset(FVector2D(2,2)).ShadowColorAndOpacity(FLinearColor::Black)]
                + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
                    [SNew(STextBlock).Text(FText::FromString(Notice->Text)).Font(Font).ColorAndOpacity(Color)
                        .ShadowOffset(FVector2D(2,2)).ShadowColorAndOpacity(FLinearColor::Black)]]);
	}
	else
	{
		// Measure and draw with retail basefont24, scaled for headset reading.
		const float FontHeight = Kind == 0 ? 34.f : 32.f;
		auto Unwrapped = ACEVRUIStyle::Text(Client, Notice->Text, FontHeight, 65536.f);
		Unwrapped->SlatePrepass(1.f);
		const float Width = FMath::Clamp(float(Unwrapped->GetDesiredSize().X) + 42.f,
			Kind == 0 ? 560.f : 460.f, Kind == 0 || Kind == 3 ? 720.f : 560.f);
		auto Body = ACEVRUIStyle::Text(Client, Notice->Text, FontHeight, Width - 42.f, Kind == 3 ? Color : ACEVRUIStyle::TextColor);
		Body->SlatePrepass(1.f);
		const float ContentHeight = Body->GetDesiredSize().Y;
		const float Height = FMath::Clamp(ContentHeight + 80.f, Kind == 0 ? 240.f : 130.f, Kind == 0 ? 360.f : 220.f);
		const float BodyHeight = Height - 80.f;
		Notice->ScrollEnd = FMath::Max(0.f, ContentHeight - BodyHeight);
		Notice->ScrollFrom = FMath::Min(PreviousOffset, Notice->ScrollEnd);
		const float LineHeight = FontHeight;
		const double Hold = Append ? 5.0 : FMath::Max(6.0, double(BodyHeight / LineHeight) * 2.0);
		Notice->ScrollStarts = Now + Hold;
		const double Remaining = Notice->ScrollEnd - Notice->ScrollFrom;
		Notice->Expires = Now + FMath::Max(12.0, FMath::Max(
			Notice->Text.Len() / 15.0 * (ContentHeight > 0 ? (ContentHeight - Notice->ScrollFrom) / ContentHeight : 1.f) + 4.0,
			Hold + Remaining / NoticeScrollSpeed + 7.0));
		FACEWorldObject Object;
		const FString Title = Kind == 3 ? TEXT("Notice") : Kind == 0 && Anchor && Client->GetWorldObject(Anchor->GetACEGuid(), Object) ? Object.Name : TEXT("Received");
		Panel->SetDrawSize(FVector2D(Width,Height));
		Panel->SetSlateWidget(ACEVRUIStyle::Frame(SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0,0,0,10)
			[ACEVRUIStyle::Text(Client, Title, 28.f, Width - 42.f, ACEVRUIStyle::Gold)]
			+ SVerticalBox::Slot().FillHeight(1.f)
			[SAssignNew(Notice->Scroll,SScrollBox).ScrollBarVisibility(EVisibility::Collapsed).AllowOverscroll(EAllowOverscroll::No)
				+ SScrollBox::Slot()[Body]], 16.f));
		Notice->Scroll->SetScrollOffset(Notice->ScrollFrom);
	}
	Panel->SetTintColorAndOpacity(FLinearColor::White); Panel->RequestRedraw();
	UpdateWorldNotices();
}

void UACEVRComponent::UpdateWorldNotices()
{
	UpdateEnemyHealthBars();
	const double Now = FPlatformTime::Seconds();
	const bool InWorld = Client && Client->GetSessionState() == EACESessionState::InWorld && !PC->IsWorldTransitionActive();
	if (!InWorld) { NoticeHealth = -1; NoticePlayerGuid = 0; }
    const FRotator ViewRotation(FMath::Clamp(Head->GetComponentRotation().Pitch,-35.f,35.f),Head->GetComponentRotation().Yaw,0);
    if (LastCombatNoticeUpdate==0 || !InWorld) CombatNoticeRotation=ViewRotation;
    const FRotator Delta=(ViewRotation-CombatNoticeRotation).GetNormalized();
    if (FMath::Abs(Delta.Yaw)>8.f || FMath::Abs(Delta.Pitch)>6.f)
        CombatNoticeRotation=FMath::RInterpTo(CombatNoticeRotation,ViewRotation,float(FMath::Min(Now-LastCombatNoticeUpdate,.1)),7.f);
    LastCombatNoticeUpdate=Now;
	for (auto& Notice : WorldNotices)
	{
		auto* Panel = Notice.Panel.Get(); if (!Panel) continue;
		if (!InWorld) Notice.Expires = 0.;
		const bool Visible = bTracking && InWorld && !PC->bEnterWorldLoading && !PC->bWorldRevealActive && Now < Notice.Expires;
		Panel->SetVisibility(Visible); if (!Visible) continue;
		if (auto* Actor = Notice.Actor.Get(); Actor && Notice.Kind!=2)
		{
			if (Actor->IsHidden() || !Actor->IsCellVisible()) { Panel->SetVisibility(false); continue; }
			Notice.Position = NoticeActorPosition(Actor);
		}
		// Preserve the last world position when a defeated actor is destroyed.
		FVector Position = Notice.Position;
		// The widget pivot is its center. Reserve the whole lower half above
		// the visible model, rather than relying on a human-sized collision pill.
		if (Notice.Actor.IsValid() && Notice.Kind < 2)
			Position.Z += Panel->GetDrawSize().Y * Panel->GetComponentScale().Z * .5f + 12.f;
		if (Notice.Kind == 3)
		{
			Position = Head->GetComponentTransform().TransformPosition(FVector(110,0,28));
		}
		else if (Notice.Kind == 2)
		{
            int32 Row=0;
            for (const auto& N:WorldNotices)
                if (&N!=&Notice && N.Kind==2 && N.NumberLane==Notice.NumberLane && N.Expires>Now
                    && (N.Started>Notice.Started || (N.Started==Notice.Started && &N>&Notice))) ++Row;
            Panel->SetWorldScale3D(FVector(.09f));
            Position=FTransform(CombatNoticeRotation,Head->GetComponentLocation()).TransformPosition(
                FVector(145,Notice.NumberLane*38.f,20.f-Row*13.f));
		}
		else if (Notice.Kind == 1 && Notice.Actor.IsValid())
		{
			const auto* Speech = WorldNotices.FindByPredicate([&](const FWorldNotice& N) { return N.Kind == 0 && N.Actor == Notice.Actor && N.Expires > Now && N.Panel.IsValid(); });
			const float SpeechWidth = Speech ? Speech->Panel->GetDrawSize().X * .1f : 72.f;
			Position += Head->GetRightVector() * (SpeechWidth * .5f + Panel->GetDrawSize().X * .05f + 8.f);
		}
		Panel->SetWorldLocationAndRotation(Position, (Head->GetComponentLocation() - Position).Rotation());
		Panel->SetTintColorAndOpacity(FLinearColor(1,1,1,FMath::Clamp(float((Notice.Expires-Now) / .7),0.f,1.f)));
		if (Notice.Scroll && Now > Notice.ScrollStarts && Now - Notice.LastRedraw >= .05)
		{
			const float Offset = FMath::Min(Notice.ScrollEnd, Notice.ScrollFrom + float(Now - Notice.ScrollStarts) * NoticeScrollSpeed);
			if (FMath::Abs(Offset - Notice.Scroll->GetScrollOffset()) > .1f)
			{
				Notice.Scroll->SetScrollOffset(Offset);
				Panel->RequestRedraw();
			}
			Notice.LastRedraw = Now;
		}
	}
	// Separate receipts even if their giver could not be resolved. Compare in
	// the viewer's plane so differing NPC distances cannot hide the dialogue.
	const FTransform View = Head->GetComponentTransform();
	for (auto& Receipt : WorldNotices)
	{
		auto* Panel = Receipt.Panel.Get();
		if (Receipt.Kind != 1 || !Panel || !Panel->IsVisible()) continue;
		FVector Local = View.InverseTransformPosition(Panel->GetComponentLocation());
		if (Local.X < 10.f) continue;
		const FVector2D Half = Panel->GetDrawSize() * (.05f * 100.f / Local.X);
		FVector2D Center(Local.Y * 100.f / Local.X, Local.Z * 100.f / Local.X);
		for (const auto& Speech : WorldNotices)
		{
			const auto* Other = Speech.Panel.Get();
			if (Speech.Kind != 0 || !Other || !Other->IsVisible()) continue;
			const FVector OtherLocal = View.InverseTransformPosition(Other->GetComponentLocation());
			if (OtherLocal.X < 10.f) continue;
			const FVector2D OtherHalf = Other->GetDrawSize() * (.05f * 100.f / OtherLocal.X);
			const FVector2D OtherCenter(OtherLocal.Y * 100.f / OtherLocal.X, OtherLocal.Z * 100.f / OtherLocal.X);
			if (FMath::Abs(Center.X - OtherCenter.X) >= Half.X + OtherHalf.X + 3.f ||
				FMath::Abs(Center.Y - OtherCenter.Y) >= Half.Y + OtherHalf.Y + 3.f) continue;
			const float Side = OtherCenter.X + OtherHalf.X + Half.X + 6.f;
			if (Side + Half.X < 75.f) Center.X = Side;
			else Center.Y = OtherCenter.Y - OtherHalf.Y - Half.Y - 6.f;
		}
		Local.Y = Center.X * Local.X / 100.f; Local.Z = Center.Y * Local.X / 100.f;
		const FVector Position = View.TransformPosition(Local);
		Panel->SetWorldLocationAndRotation(Position, (View.GetLocation() - Position).Rotation());
	}
}

void UACEVRComponent::EnemyHealth(int32 Guid, float Fraction)
{
	if (!bActive || !Client || Guid==Client->GetPlayerGuid() || !FMath::IsFinite(Fraction)
		|| !PC || !PresentationActor || PC->IsWorldTransitionActive()) return;
	AACEWorldEntityActor* Actor=nullptr;
	for (TActorIterator<AACEWorldEntityActor> It(GetWorld()); It; ++It)
		if (It->GetACEGuid()==Guid && !It->IsCorpse() && (It->ItemType & ACEItemType::Creature)) { Actor=*It; break; }
	if (!Actor) return;
	const double Now=FPlatformTime::Seconds();
	auto* Bar=EnemyHealthBars.FindByPredicate([&](const auto& B) { return B.Actor==Actor; });
	// Death can arrive before the corpse/create update, including a one-hit kill.
	// Never allocate or briefly draw an empty black meter for that event.
	if (Fraction<=0.f)
	{
		if (Bar) { Bar->Expires=0; Bar->Actor.Reset(); if (Bar->Panel.IsValid()) Bar->Panel->SetVisibility(false); }
		return;
	}
	// Do not fill the landscape with health bars for unhurt creatures.
	if (!Bar && Fraction>=1.f) return;
	if (!Bar) Bar=EnemyHealthBars.FindByPredicate([&](const auto& B) { return B.Expires<=Now || !B.Actor.IsValid(); });
	if (!Bar && EnemyHealthBars.Num()<16) Bar=&EnemyHealthBars.AddDefaulted_GetRef();
	if (!Bar) return;
	if (!Bar->Panel.IsValid())
	{
		// Each enemy owns a smooth vector bar, independent of selection and the
		// low-resolution native toolbar. No per-frame canvas rebuild or font work.
		Bar->Meter=SNew(SACEEnemyHealthBar);
		auto* Panel=NewObject<UWidgetComponent>(PresentationActor); PresentationActor->AddInstanceComponent(Panel);
		Panel->SetupAttachment(PresentationActor->GetRootComponent()); Panel->SetWidgetSpace(EWidgetSpace::World);
		Panel->SetDrawSize(FVector2D(512,40));
		Panel->SetSlateWidget(Bar->Meter); Panel->SetBlendMode(EWidgetBlendMode::Transparent); Panel->SetTwoSided(true);
		Panel->SetBackgroundColor(FLinearColor::Transparent);
		Panel->SetCollisionEnabled(ECollisionEnabled::NoCollision); Panel->SetCastShadow(false);
		Panel->SetTickWhenOffscreen(true);
		Panel->SetManuallyRedraw(true); Panel->RegisterComponent(); Bar->Panel=Panel;
	}
	Bar->Actor=Actor; Bar->Meter->SetFraction(Fraction);
	Bar->Expires=Now+15.;
	Bar->RedrawsRemaining=1;
	Bar->Panel->RequestRedraw(); UpdateEnemyHealthBars();
}

void UACEVRComponent::UpdateEnemyHealthBars()
{
	const double Now=FPlatformTime::Seconds();
	const bool InWorld=Client && Client->GetSessionState()==EACESessionState::InWorld && !PC->IsWorldTransitionActive();
	for (auto& Bar:EnemyHealthBars)
	{
		auto* Panel=Bar.Panel.Get(); if (!Panel) continue;
		if (!InWorld) { Bar.Expires=0; Bar.Actor.Reset(); }
		auto* Actor=Bar.Actor.Get();
		const bool Visible=InWorld && bTracking && Actor && !Actor->IsCorpse() && Bar.Meter && Bar.Meter->GetFraction()>0.f
			&& !Actor->IsHidden() && Actor->IsCellVisible() && Now<Bar.Expires
			&& FVector::DistSquared(Actor->GetActorLocation(),Head->GetComponentLocation())<FMath::Square(5000.f);
		Panel->SetVisibility(Visible); if (!Visible) continue;
		if (Bar.RedrawsRemaining>0) { --Bar.RedrawsRemaining; Panel->RequestRedraw(); }
		FVector Position=NoticeActorPosition(Actor);
		const float Distance=FVector::Distance(Position,Head->GetComponentLocation());
		// Preserve roughly eight degrees of readable width, including distant
		// targets; the old 160 cm cap made their bars shrink to a few pixels.
		const float Width=FMath::Clamp(Distance*.14f,36.f,700.f);
		const float Scale=Width/FMath::Max(1.f,float(Panel->GetDrawSize().X));
		Position.Z+=12.f+Panel->GetDrawSize().Y*Scale*.5f;
		Panel->SetWorldScale3D(FVector(Scale));
		Panel->SetWorldLocationAndRotation(Position,(Head->GetComponentLocation()-Position).Rotation());
	}
}
