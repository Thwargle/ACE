using System;

using ACE.Entity;
using ACE.Server.Managers;
using ACE.Server.WorldObjects;

namespace ACE.Server.Entity
{
    public class DamageHistoryInfo
    {
        public readonly WeakReference<WorldObject> Attacker;

        public readonly ObjectGuid Guid;
        public readonly string Name;

        public float TotalDamage;

        public readonly WeakReference<Player> PetOwner;

        public bool IsPlayer => Guid.IsPlayer();

        public readonly bool IsOlthoiPlayer;

        public DamageHistoryInfo(WorldObject attacker, float totalDamage = 0.0f)
        {
            Attacker = new WeakReference<WorldObject>(attacker);

            Guid = attacker.Guid;
            Name = attacker.Name;

            IsOlthoiPlayer = attacker is Player player && player.IsOlthoiPlayer;

            TotalDamage = totalDamage;

            // CombatPet and passive Pet both set P_PetOwner; also resolve via PetOwner instance id
            // so kill XP still credits the summoner if the typed reference was lost.
            if (attacker is Pet pet && pet.P_PetOwner != null)
                PetOwner = new WeakReference<Player>(pet.P_PetOwner);
            else if (attacker.PetOwner != null)
            {
                var owner = PlayerManager.GetOnlinePlayer(attacker.PetOwner.Value);
                if (owner != null)
                    PetOwner = new WeakReference<Player>(owner);
            }
        }

        public WorldObject TryGetAttacker()
        {
            Attacker.TryGetTarget(out var attacker);

            return attacker;
        }

        public Player TryGetPetOwner()
        {
            PetOwner.TryGetTarget(out var petOwner);

            return petOwner;
        }

        public WorldObject TryGetPetOwnerOrAttacker()
        {
            if (PetOwner != null)
                return TryGetPetOwner();
            else
                return TryGetAttacker();
        }
    }
}
