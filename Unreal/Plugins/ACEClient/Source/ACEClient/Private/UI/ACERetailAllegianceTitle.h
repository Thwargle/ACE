#pragma once
#include "CoreMinimal.h"

// Retail AllegianceSystem::GetTitle and its heritage/gender title tables.
// Unknown rank, heritage or gender leaves the name without a rank prefix.
namespace ACERetailAllegiance
{
inline const TCHAR* Title(int32 Rank, int32 Heritage, int32 Gender)
{
    if (Rank < 1 || Rank > 10 || (Gender != 1 && Gender != 2)) return TEXT("");
    static const TCHAR* AluvianMale[] = {TEXT("Yeoman"), TEXT("Baronet"), TEXT("Baron"), TEXT("Reeve"), TEXT("Thane"), TEXT("Ealdor"), TEXT("Duke"), TEXT("Aetheling"), TEXT("King"), TEXT("High King")};
    static const TCHAR* AluvianFemale[] = {TEXT("Yeoman"), TEXT("Baronet"), TEXT("Baroness"), TEXT("Reeve"), TEXT("Thane"), TEXT("Ealdor"), TEXT("Duchess"), TEXT("Aetheling"), TEXT("Queen"), TEXT("High Queen")};
    static const TCHAR* GharundimMale[] = {TEXT("Sayyid"), TEXT("Shayk"), TEXT("Maulan"), TEXT("Mu'allim"), TEXT("Naquib"), TEXT("Qadi"), TEXT("Mushir"), TEXT("Amir"), TEXT("Malik"), TEXT("Sultan")};
    static const TCHAR* GharundimFemale[] = {TEXT("Sayyida"), TEXT("Shayka"), TEXT("Maulana"), TEXT("Mu'allima"), TEXT("Naquiba"), TEXT("Qadiya"), TEXT("Mushira"), TEXT("Amira"), TEXT("Malika"), TEXT("Sultana")};
    static const TCHAR* ShoMale[] = {TEXT("Jinin"), TEXT("Jo-chueh"), TEXT("Nan-chueh"), TEXT("Shi-chueh"), TEXT("Ta-chueh"), TEXT("Kun-chueh"), TEXT("Kou"), TEXT("Taikou"), TEXT("Ou"), TEXT("Koutei")};
    static const TCHAR* ShoFemale[] = {TEXT("Jinin"), TEXT("Jo-chueh"), TEXT("Nan-chueh"), TEXT("Shi-chueh"), TEXT("Ta-chueh"), TEXT("Kun-chueh"), TEXT("Kou"), TEXT("Taikou"), TEXT("Jo-ou"), TEXT("Koutei")};
    static const TCHAR* ViamontianMale[] = {TEXT("Squire"), TEXT("Banner"), TEXT("Baron"), TEXT("Viscount"), TEXT("Count"), TEXT("Marquis"), TEXT("Duke"), TEXT("Grand Duke"), TEXT("King"), TEXT("High King")};
    static const TCHAR* ViamontianFemale[] = {TEXT("Dame"), TEXT("Banner"), TEXT("Baroness"), TEXT("Viscountess"), TEXT("Countess"), TEXT("Marquise"), TEXT("Duchess"), TEXT("Grand Duchess"), TEXT("Queen"), TEXT("High Queen")};
    static const TCHAR* ShadowboundMale[] = {TEXT("Tenebrous"), TEXT("Shade"), TEXT("Squire"), TEXT("Knight"), TEXT("Void Knight"), TEXT("Void Lord"), TEXT("Duke"), TEXT("Archduke"), TEXT("Highborn"), TEXT("King")};
    static const TCHAR* ShadowboundFemale[] = {TEXT("Tenebrous"), TEXT("Shade"), TEXT("Squire"), TEXT("Knight"), TEXT("Void Knight"), TEXT("Void Lady"), TEXT("Duchess"), TEXT("Archduchess"), TEXT("Highborn"), TEXT("Queen")};
    static const TCHAR* GearknightMale[] = {TEXT("Tribunus"), TEXT("Praefectus"), TEXT("Optio"), TEXT("Centurion"), TEXT("Principes"), TEXT("Legatus"), TEXT("Consul"), TEXT("Dux"), TEXT("Secondus"), TEXT("Primus")};
    static const TCHAR* TumerokMale[] = {TEXT("Xutua"), TEXT("Tuona"), TEXT("Ona"), TEXT("Nuona"), TEXT("Turea"), TEXT("Rea"), TEXT("Nurea"), TEXT("Kauh"), TEXT("Sutah"), TEXT("Tah")};
    static const TCHAR* LugianMale[] = {TEXT("Laigus"), TEXT("Raigus"), TEXT("Amploth"), TEXT("Arintoth"), TEXT("Obeloth"), TEXT("Lithos"), TEXT("Kantos"), TEXT("Gigas"), TEXT("Extas"), TEXT("Tiatus")};
    static const TCHAR* EmpyreanMale[] = {TEXT("Ensign"), TEXT("Corporal"), TEXT("Lieutenant"), TEXT("Commander"), TEXT("Captain"), TEXT("Commodore"), TEXT("Admiral"), TEXT("Warlord"), TEXT("Ipharsin"), TEXT("Aulin")};
    static const TCHAR* EmpyreanFemale[] = {TEXT("Ensign"), TEXT("Corporal"), TEXT("Lieutenant"), TEXT("Commander"), TEXT("Captain"), TEXT("Commodore"), TEXT("Admiral"), TEXT("Warlord"), TEXT("Ipharsia"), TEXT("Aulia")};
    static const TCHAR* UndeadMale[] = {TEXT("Neophyte"), TEXT("Acolyte"), TEXT("Adept"), TEXT("Esquire"), TEXT("Squire"), TEXT("Knight"), TEXT("Count"), TEXT("Viscount"), TEXT("Highness"), TEXT("Annointed")};
    static const TCHAR* UndeadFemale[] = {TEXT("Neophyte"), TEXT("Acolyte"), TEXT("Adept"), TEXT("Esquire"), TEXT("Squire"), TEXT("Knight"), TEXT("Countess"), TEXT("Viscountess"), TEXT("Highness"), TEXT("Annointed")};
    const TCHAR* const* Titles = nullptr;
    switch (Heritage)
    {
    case 1: Titles = Gender == 1 ? AluvianMale : AluvianFemale; break;
    case 2: Titles = Gender == 1 ? GharundimMale : GharundimFemale; break;
    case 3: Titles = Gender == 1 ? ShoMale : ShoFemale; break;
    case 4: Titles = Gender == 1 ? ViamontianMale : ViamontianFemale; break;
    case 5: case 10: Titles = Gender == 1 ? ShadowboundMale : ShadowboundFemale; break;
    case 6: Titles = GearknightMale; break;
    case 7: Titles = TumerokMale; break;
    case 8: Titles = LugianMale; break;
    case 9: Titles = Gender == 1 ? EmpyreanMale : EmpyreanFemale; break;
    case 11: Titles = Gender == 1 ? UndeadMale : UndeadFemale; break;
    default: return TEXT("");
    }
    return Titles[Rank - 1];
}
}
