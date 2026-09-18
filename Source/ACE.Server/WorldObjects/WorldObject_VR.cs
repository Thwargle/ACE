namespace ACE.Server.WorldObjects
{
    partial class WorldObject
    {
        // Server-only projectile metadata. Never read from client object properties or persisted in biota.
        public bool IsVRFreeAimProjectile { get; set; }
        public uint? VRMissileAttackSkill { get; set; }
        public float? VRMissileAccuracy { get; set; }
    }
}
