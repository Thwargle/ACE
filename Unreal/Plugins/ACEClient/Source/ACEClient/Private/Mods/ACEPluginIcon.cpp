#include "Mods/ACEPluginIcon.h"
#include "Rendering/DrawElements.h"

void SACEPluginIcon::Construct(const FArguments& Args)
{
    SetCanTick(false);SetVisibility(EVisibility::HitTestInvisible);
    const FLinearColor Gold(.94f,.68f,.26f),Paper(.96f,.88f,.65f),Teal(.32f,.76f,.65f),Red(.94f,.36f,.22f);
    auto Line=[&](std::initializer_list<FVector2D> Points,FLinearColor Color)
    {FPath P;P.Color=Color;for(auto V:Points)P.Points.Add(V);Paths.Add(MoveTemp(P));};
    auto Fill=[&](std::initializer_list<FVector2D> Points,FLinearColor Color)
    {Line(Points,Color);Paths.Last().Filled=true;};
    const FString& Id=Args._Id;
    if(Id==TEXT("plugin.options")) // Gold eight-tooth gear around a teal hub.
    {
        FPath Gear;Gear.Color=Gold;
        for(int32 I=0;I<=32;++I)
        {
            const double Angle=(I-.5)*PI/16,Radius=(I%4<2)?13.:10.;
            Gear.Points.Add(FVector2D(16+Radius*FMath::Cos(Angle),16+Radius*FMath::Sin(Angle)));
        }
        Paths.Add(MoveTemp(Gear));
        FPath Hub;Hub.Color=Teal;
        for(int32 I=0;I<=16;++I){const double Angle=I*PI/8;Hub.Points.Add(FVector2D(16+5*FMath::Cos(Angle),16+5*FMath::Sin(Angle)));}
        Paths.Add(MoveTemp(Hub));Fill({{16,13},{19,16},{16,19},{13,16}},Paper);
    }
    else if(Id==TEXT("plugin.grip"))
    {
        for(int X:{9,21})for(int Y:{6,16,26})Fill({{X-2.,Y-2.},{X+2.,Y-2.},{X+2.,Y+2.},{X-2.,Y+2.}},Gold);
    }
    else if(Id==TEXT("ucm")) // Two blades around a warding rune.
    {
        Fill({{5,3},{10,6},{24,24},{22,26},{7,9}},Paper);
        Fill({{27,3},{22,6},{8,24},{10,26},{25,9}},Gold);
        Line({{4,22},{11,28}},Gold);Line({{21,28},{28,22}},Gold);
        Line({{7,26},{4,29}},Paper);Line({{25,26},{28,29}},Paper);
        Fill({{16,10},{21,15},{16,21},{11,15}},Teal);
    }
    else if(Id==TEXT("ucmmicro")) // Three compact switches.
    {
        for(int32 I=0;I<3;++I)
        {
            const double Y=6+I*10;Line({{3,Y},{29,Y}},Gold);
            const double X=I==1?10:22;Fill({{X-3,Y-3},{X+3,Y-3},{X+3,Y+3},{X-3,Y+3}},I==1?Paper:Teal);
        }
    }
    else if(Id==TEXT("actiondeck")) // Gold hotbar tiles with a teal action spark.
    {
        for(int32 Y=0;Y<3;++Y)for(int32 X=0;X<3;++X)
        {
            const double L=3+X*9,T=4+Y*9;
            Line({{L,T},{L+7,T},{L+7,T+7},{L,T+7},{L,T}},Gold);
        }
        Fill({{19,1},{12,15},{17,15},{13,30},{25,12},{19,12}},Teal);
        Line({{19,3},{15,13},{20,13}},Paper);
    }
    else if(Id==TEXT("supplyledger")) // Three tapers tied with a counting band.
    {
        Fill({{5,11},{10,11},{10,28},{5,28}},Paper);Fill({{13,6},{18,6},{18,28},{13,28}},Gold);
        Fill({{21,14},{26,14},{26,28},{21,28}},Paper);
        Line({{3,21},{28,21},{28,25},{3,25},{3,21}},Teal);
        Fill({{7,4},{9,7},{7,9},{5,7}},Red);Fill({{15,1},{17,3},{15,5},{13,3}},Teal);
        Fill({{23,7},{25,10},{23,12},{21,10}},Red);
    }
    else if(Id==TEXT("huntledger")) // Hourglass and rising gains.
    {
        Line({{3,3},{17,3},{17,6},{5,6},{5,10},{13,19},{15,23},{15,27},{3,27}},Gold);
        Line({{15,6},{15,10},{7,19},{5,23},{5,27},{17,27}},Gold);
        Fill({{7,8},{13,8},{10,12}},Paper);Fill({{10,20},{14,25},{6,25}},Paper);
        Line({{20,27},{20,21},{24,21},{24,15},{28,15},{28,8}},Teal);
        Line({{24,11},{28,7},{31,11}},Paper);
    }
    else if(Id==TEXT("looteditor")) // Treasure chest with a jewel.
    {
        Line({{3,17},{3,11},{6,7},{24,7},{28,11},{28,17},{3,17},{3,28},{28,28},{28,17}},Gold);
        Line({{8,8},{8,16}},Paper);Line({{23,8},{23,16}},Paper);
        Fill({{16,13},{21,18},{16,23},{11,18}},Teal);Line({{16,14},{16,22}},Paper);
    }
    else if(Id==TEXT("itemtools")) // Parcel passing between two hands/arrows.
    {
        Line({{3,10},{3,5},{25,5},{21,2}},Gold);Line({{25,5},{21,8}},Gold);
        Line({{29,22},{29,27},{7,27},{11,30}},Teal);Line({{7,27},{11,24}},Teal);
        Line({{8,12},{16,8},{24,12},{24,21},{16,25},{8,21},{8,12},{16,16},{24,12}},Paper);
        Line({{16,16},{16,25}},Gold);
    }
    else if(Id==TEXT("autocraft")) // Mortar, pestle and a small crafting spark.
    {
        Fill({{5,14},{27,14},{24,24},{20,28},{11,28},{7,24}},Teal);
        Line({{4,14},{28,14},{25,23},{20,28},{11,28},{7,23},{4,14}},Gold);
        Fill({{18,16},{15,13},{23,3},{27,6}},Paper);
        Line({{9,29},{23,29}},Gold);
        Line({{7,2},{7,10}},Gold);Line({{3,6},{11,6}},Gold);
    }
    else if(Id==TEXT("foundry")) // Anvil, blueprint diamond and a rising forge spark.
    {
        Fill({{3,16},{28,16},{24,21},{19,21},{19,26},{25,28},{6,28},{12,25},{12,21},{6,21}},Teal);
        Line({{3,16},{28,16},{24,21},{19,21},{19,26},{25,28},{6,28}},Gold);
        Line({{6,5},{12,2},{18,5},{18,11},{12,14},{6,11},{6,5},{12,8},{18,5}},Paper);
        Line({{12,8},{12,14}},Gold);Line({{25,2},{25,11}},Gold);Line({{21,6},{29,6}},Paper);
    }
    else if(Id==TEXT("derethlens")) // Faceted lens over a mountain and a waterline.
    {
        Line({{5,8},{12,2},{23,4},{28,14},{23,23},{12,25},{5,18},{5,8}},Gold);
        Fill({{7,18},{14,8},{18,15},{21,11},{26,18}},Teal);
        Line({{8,20},{14,19},{20,21},{24,20}},Paper);
        Line({{23,24},{29,30}},Gold);Line({{20,25},{26,31}},Paper);
    }
    else if(Id==TEXT("thirdeye")) // An open eye beneath a third sight ray.
    {
        Line({{2,18},{7,12},{12,9},{20,9},{25,12},{30,18},{25,23},{20,26},{12,26},{7,23},{2,18}},Gold);
        Line({{16,10},{22,14},{22,21},{16,25},{10,21},{10,14},{16,10}},Teal);
        Fill({{16,13},{19,17},{16,22},{13,17}},Paper);
        Line({{16,1},{16,6}},Gold);Line({{6,3},{9,7}},Paper);Line({{26,3},{23,7}},Paper);
    }
    else if(Id==TEXT("waypoint")) // Trail terminating in a north needle.
    {
        Line({{3,28},{10,28},{10,21},{18,21},{18,14}},Gold);
        Fill({{20,2},{28,19},{20,15}},Paper);Fill({{20,2},{12,19},{20,15}},Teal);
        Line({{4,24},{4,29},{8,29}},Paper);
    }
    else if(Id==TEXT("waypoint.map")) // Folded parchment and location diamond.
    {
        Line({{3,7},{11,4},{21,8},{29,5},{29,26},{21,29},{11,25},{3,28},{3,7}},Gold);
        Line({{11,5},{11,24}},Paper);Line({{21,9},{21,28}},Paper);
        Fill({{17,11},{22,16},{17,22},{12,16}},Teal);Fill({{17,14},{19,16},{17,18},{15,16}},Paper);
    }
    else if(Id==TEXT("ucm.log")) // Scroll and a writing nib.
    {
        Line({{25,9},{25,4},{5,4},{3,6},{3,10},{7,10},{7,25},{10,28},{27,28},{29,25},{29,23},{10,23},{10,26}},Gold);
        Line({{10,9},{18,9}},Paper);Line({{10,13},{17,13}},Paper);Line({{10,17},{14,17}},Paper);
        Fill({{27,9},{30,12},{20,22},{17,23},{18,19}},Teal);
    }
    else if(Id.Contains(TEXT("wow"))||Id.Contains(TEXT("transmog"))) // Faceted mask.
    {
        Line({{3,8},{9,4},{16,7},{23,4},{29,8},{25,23},{16,29},{7,23},{3,8}},Gold);
        Fill({{6,12},{13,13},{11,17},{7,16}},Teal);Fill({{26,12},{19,13},{21,17},{25,16}},Teal);
        Line({{16,12},{14,22},{18,22}},Paper);
    }
    else // Extension sigil for third-party plugins.
    {
        Line({{16,2},{29,16},{16,30},{3,16},{16,2}},Gold);
        Line({{10,10},{10,22},{22,22},{22,10},{10,10}},Teal);
        Fill({{16,11},{21,16},{16,21},{11,16}},Paper);
    }
}
FSlateLayoutTransform SACEPluginIcon::Fit(const FVector2D& Size)
{
    const float Side=FMath::Max(0.,FMath::Min(Size.X,Size.Y));
    return FSlateLayoutTransform(Side/32.f,(Size-FVector2D(Side,Side))*.5);
}
int32 SACEPluginIcon::OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle& Style,bool Enabled) const
{
    const auto Art=G.MakeChild(FVector2D(32,32),Fit(G.GetLocalSize()));
    const auto Effect=ShouldBeEnabled(Enabled)?ESlateDrawEffect::None:ESlateDrawEffect::DisabledEffect;
    for(const auto& P:Paths)
    {
        const auto Color=P.Color*Style.GetColorAndOpacityTint();
        if(!P.Filled)FSlateDrawElement::MakeLines(Out,Layer,Art.ToPaintGeometry(),P.Points,Effect,Color,true,1.7f);
        else
        {
            TArray<FSlateVertex,TInlineAllocator<8>> Vertices;TArray<SlateIndex,TInlineAllocator<18>> Indices;
            for(auto V:P.Points)Vertices.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(Art.GetAccumulatedRenderTransform(),FVector2f(V),FVector2f::ZeroVector,Color.ToFColor(false)));
            for(int32 I=1;I<P.Points.Num()-1;++I){Indices.Add(0);Indices.Add(I);Indices.Add(I+1);}
            FSlateDrawElement::MakeCustomVerts(Out,Layer,FSlateResourceHandle(),TArray<FSlateVertex>(Vertices),TArray<SlateIndex>(Indices),nullptr,0,0,Effect);
        }
    }
    return Layer;
}
