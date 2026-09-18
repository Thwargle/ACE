using ACE.DatLoader.Entity;
using ACE.DatLoader.FileTypes;
using ACE.Entity.Enum;

// Retail LayoutDesc::InqFullDesc (0069DFB0), ElementDesc::Incorporate (0069B050),
// StateDesc::Incorporate (0069CC80), and StateDesc::UpdateSizeAndPosition (0069BF20).
// Keep this separate from manifest formatting so the same resolver is exercised by tests.
internal static class RetailLayoutResolver
{
    public static StateDesc CloneState(StateDesc source)
    {
        var copy = new StateDesc { StateId = source.StateId, PassToChildren = source.PassToChildren,
            UiIncorporationFlags = source.UiIncorporationFlags };
        copy.Media.AddRange(source.Media);
        foreach (var p in source.Properties) copy.Properties[p.Key] = p.Value;
        return copy;
    }

    public static ElementDesc Clone(ElementDesc source)
    {
        var copy = new ElementDesc {
            StateId = source.StateId, PassToChildren = source.PassToChildren,
            UiIncorporationFlags = source.UiIncorporationFlags, UiReadOrder = source.UiReadOrder,
            ElementId = source.ElementId, Type = source.Type, BaseElement = source.BaseElement,
            BaseLayout = source.BaseLayout, DefaultState = source.DefaultState,
            X = source.X, Y = source.Y, Width = source.Width, Height = source.Height,
            ZLevel = source.ZLevel, LeftEdge = source.LeftEdge, TopEdge = source.TopEdge,
            RightEdge = source.RightEdge, BottomEdge = source.BottomEdge };
        copy.Media.AddRange(source.Media);
        foreach (var p in source.Properties) copy.Properties[p.Key] = p.Value;
        foreach (var state in source.States) copy.States[state.Key] = CloneState(state.Value);
        foreach (var child in source.Children) copy.Children[child.Key] = Clone(child.Value);
        return copy;
    }

    private static void IncorporateState(StateDesc target, StateDesc overlay)
    {
        target.UiIncorporationFlags |= overlay.UiIncorporationFlags;
        if (overlay.UiIncorporationFlags.HasFlag(IncorporationFlags.PassToChildren))
            target.PassToChildren = overlay.PassToChildren;
        // Media is an ordered program, not a single fallback image.
        target.Media.AddRange(overlay.Media);
        foreach (var p in overlay.Properties) target.Properties[p.Key] = p.Value;
    }

    public static void Incorporate(ElementDesc target, ElementDesc overlay)
    {
        uint oldW = target.Width, oldH = target.Height;
        var flags = overlay.UiIncorporationFlags;
        IncorporateState(target, overlay);
        if (flags.HasFlag(IncorporationFlags.X)) target.X = overlay.X;
        if (flags.HasFlag(IncorporationFlags.Y)) target.Y = overlay.Y;
        if (flags.HasFlag(IncorporationFlags.Width)) target.Width = overlay.Width;
        if (flags.HasFlag(IncorporationFlags.Height)) target.Height = overlay.Height;
        if (flags.HasFlag(IncorporationFlags.ZLevel)) target.ZLevel = overlay.ZLevel;
        // Type comes from the concrete base. A type-zero overlay is an instance.
        target.ElementId = overlay.ElementId;
        target.DefaultState = overlay.DefaultState;
        target.LeftEdge = overlay.LeftEdge; target.TopEdge = overlay.TopEdge;
        target.RightEdge = overlay.RightEdge; target.BottomEdge = overlay.BottomEdge;
        target.UiReadOrder = overlay.UiReadOrder;
        foreach (var state in overlay.States)
        {
            if (target.States.TryGetValue(state.Key, out var inherited))
                IncorporateState(inherited, state.Value);
            else target.States[state.Key] = CloneState(state.Value);
        }
        uint inheritedCount = (uint)target.Children.Keys.Count(k => !overlay.Children.ContainsKey(k));
        foreach (var child in overlay.Children)
        {
            if (target.Children.TryGetValue(child.Key, out var inherited))
                Incorporate(inherited, child.Value);
            else target.Children[child.Key] = Clone(child.Value);
            target.Children[child.Key].UiReadOrder += inheritedCount;
        }
        if (oldW != target.Width || oldH != target.Height)
            foreach (var child in target.Children)
                if (!overlay.Children.ContainsKey(child.Key))
                    Reflow(child.Value, oldW, oldH, target.Width, target.Height);
    }

    public static void Reflow(ElementDesc node, uint oldW, uint oldH, uint newW, uint newH)
    {
        // Retail boxes use inclusive right/bottom edges and integer truncation.
        static (uint, uint) Axis(uint start, uint size, uint before, uint after, uint lo, uint hi)
        {
            long a = unchecked((int)start), b = a + size - 1;
            long delta = (long)after - before;
            float ratio = before != 0 ? (float)after / before : 0;
            long l = lo switch { 2 => a + delta, 3 => after / 2 - (long)(size / 2),
                4 => (long)(a * ratio), _ => a };
            long r = hi switch { 1 => b + delta, 3 => after / 2 + (long)(size / 2) - 1,
                4 => (long)(b * ratio), _ => b };
            return (unchecked((uint)l), (uint)Math.Max(0, r - l + 1));
        }
        uint w = node.Width, h = node.Height;
        (node.X, node.Width) = Axis(node.X, w, oldW, newW, node.LeftEdge, node.RightEdge);
        (node.Y, node.Height) = Axis(node.Y, h, oldH, newH, node.TopEdge, node.BottomEdge);
        foreach (var child in node.Children.Values) Reflow(child, w, h, node.Width, node.Height);
    }

    public static ElementDesc Resolve(ElementDesc source, IReadOnlyDictionary<uint, LayoutDesc> layouts)
    {
        ElementDesc Full(ElementDesc node, HashSet<(uint, uint)> path)
        {
            if (node.Type != 0) return Clone(node);
            var key = (node.BaseLayout, node.BaseElement);
            if (!path.Add(key)) throw new InvalidDataException($"Layout inheritance cycle {key}");
            if (!layouts.TryGetValue(node.BaseLayout, out var layout))
                throw new InvalidDataException($"Missing base layout {node.BaseLayout:X8}");
            // AccessElementDesc is a direct lookup in the layout's root hash table.
            if (!layout.Elements.TryGetValue(node.BaseElement, out var template))
                throw new InvalidDataException($"Missing base element {node.BaseElement:X8} in {node.BaseLayout:X8}");
            var result = Full(template, path);
            Incorporate(result, node);
            path.Remove(key);
            return result;
        }
        ElementDesc Tree(ElementDesc node)
        {
            var result = Full(node, new());
            foreach (var key in result.Children.Keys.ToArray()) result.Children[key] = Tree(result.Children[key]);
            return result;
        }
        return Tree(source);
    }

    public static void RunTests()
    {
        static void Check(bool ok, string what) { if (!ok) throw new Exception(what); }
        var template = new ElementDesc { ElementId = 1, Type = 2, Width = 400, Height = 100 };
        template.Properties[17] = new BaseProperty { ValueInt = 7 };
        template.States[3] = new StateDesc { StateId = 3, PassToChildren = true };
        template.Children[2] = new ElementDesc { ElementId = 2, Type = 2, X = 397, Width = 3,
            Height = 10, LeftEdge = 2, RightEdge = 1 };
        var overlay = new ElementDesc { ElementId = 8, Width = 306, UiIncorporationFlags = IncorporationFlags.Width };
        var copy = Clone(template);
        Incorporate(copy, overlay);
        Check(copy.Type == 2 && copy.Properties[17].ValueInt == 7 && copy.States.ContainsKey(3), "Inherited type/properties/states");
        Check(copy.Children[2].X == 303 && copy.Children[2].Width == 3, "Resize inherited endcap to instance width");
        Check(template.Children[2].X == 397, "Template must remain immutable");
        var overrideChild = new ElementDesc { ElementId = 2, X = 20, UiIncorporationFlags = IncorporationFlags.X };
        overlay.Children[2] = overrideChild;
        copy = Clone(template);
        Incorporate(copy, overlay);
        Check(copy.Children[2].X == 20 && copy.Children[2].Type == 2, "Recursive child incorporation");
        Console.WriteLine("PASS: retail layout inheritance, properties, states, child reflow, immutability, overrides");
    }
}
