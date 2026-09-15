using System.Windows.Media;
using System.Windows.Media.Imaging;

namespace Stellar.Editor;

// Decorative unresolved stellar light. This never creates simulation systems or IDs.
public static class GalaxyArtwork
{
    public const int Resolution = 2048;
    public static BitmapSource Build(long seed, CancellationToken cancellation = default)
    {
        const int size = Resolution;
        var red = new float[size * size];
        var green = new float[size * size];
        var blue = new float[size * size];
        var transmission = new byte[size * size];
        var salt = unchecked((uint)(seed ^ (seed >> 32))) ^ 0x3F24D991u;
        Parallel.For(0, size, new ParallelOptions { CancellationToken = cancellation, MaxDegreeOfParallelism = Math.Max(1, Math.Min(4, Environment.ProcessorCount - 1)) }, py =>
        {
            var y = (py + .5) / size * 2.24 - 1.12;
            for (var px = 0; px < size; px++)
            {
                var x = (px + .5) / size * 2.24 - 1.12;
                var r = Math.Sqrt(x * x + y * y);
                if (r > 1.10) continue;
                var i = py * size + px;
                var theta = Math.Atan2(y, x);
                var broad = Fbm(x * 8 + 47, y * 8 + 53, salt);
                var fine = Fbm(x * 42 + 71, y * 42 + 31, salt + 9);
                var detail = Fbm(x * 138 + 11, y * 138 + 79, salt + 19);
                var turbulence = (broad - .5) * .72 + (fine - .5) * .18;
                // Same four-arm winding and bar angle as the compact native generator.
                var phase = theta - r * Math.PI * 2.35 + turbulence;
                var distance = Math.Atan2(Math.Sin(phase * 4), Math.Cos(phase * 4)) / 4;
                var width = (.13 + .024 / Math.Max(.15, r)) * (.60 + broad);
                var arms = Math.Exp(-distance * distance / (2 * width * width));
                // Two dominant arms and two weaker branches avoid uniform concentric bands.
                var armWeight = .23 + .77 * Smooth(-.35, .70, Math.Cos(phase * 2));
                var dustDistance = distance + .074 + (fine - .5) * .15 + (detail - .5) * .06;
                var dust = Math.Exp(-dustDistance * dustDistance / (.0008 + r * .0022));
                var inner = Smooth(.12, .27, r);
                var edge = 1 - Smooth(.82, 1.07, r);
                var clouds = .19 + Math.Pow(broad, 2.1) * 2.8 + Math.Pow(fine, 3.2) * 3.0;
                var dustClumps = .16 + Smooth(.28, .70, fine) * .84;
                var t = Math.Exp(-3.0 * dust * inner * dustClumps * (.45 + broad) * (.4 + .6 * armWeight)) * (.66 + detail * .58);
                transmission[i] = (byte)(Math.Clamp(t, .05, 1) * 255);
                var disk = .024 * Math.Exp(-r * 2.3) * edge * (.50 + broad * .95);
                var armLight = .26 * arms * armWeight * Math.Exp(-r * 1.65) * clouds * inner * edge;
                var bulge = .64 * Math.Exp(-Math.Pow(r / .108, 1.30));
                var barX = x * Math.Cos(-.26) + y * Math.Sin(-.26);
                var barY = -x * Math.Sin(-.26) + y * Math.Cos(-.26);
                var bar = .065 * Math.Exp(-Math.Pow(barX / .32, 4) - Math.Pow(barY / .065, 2));
                var knots = .15 * arms * armWeight * Math.Pow(Math.Max(0, fine - .49) * 3, 3) * Math.Pow(detail, 1.8) * inner * edge;
                red[i] = (float)((disk * .90 + armLight * .61 + bulge * 1.08 + bar + knots * 1.6) * Math.Pow(t, .74));
                green[i] = (float)((disk * .88 + armLight * .80 + bulge * .84 + bar * .79 + knots * .32) * t);
                blue[i] = (float)((disk + armLight + bulge * .56 + bar * .55 + knots * .76) * Math.Pow(t, 1.26));
            }
        });
        var random = new Random(unchecked((int)salt));
        for (var n = 0; n < 115000; n++)
        {
            if ((n & 1023) == 0) cancellation.ThrowIfCancellationRequested();
            var kind = random.NextDouble();
            double x, y, r;
            if (kind < .72)
            {
                r = .16 + .82 * Math.Sqrt(random.NextDouble());
                var theta = random.Next(4) * Math.PI / 2 + r * Math.PI * 2.35 + Normal(random) * .12;
                x = r * Math.Cos(theta); y = r * Math.Sin(theta);
            }
            else if (kind < .89)
            {
                x = Normal(random) * .083; y = Normal(random) * .083;
                r = Math.Sqrt(x * x + y * y);
            }
            else
            {
                r = Math.Sqrt(random.NextDouble()) * 1.03;
                var angle = random.NextDouble() * Math.Tau;
                x = r * Math.Cos(angle); y = r * Math.Sin(angle);
            }
            var cx = (int)((x / 2.24 + .5) * size);
            var cy = (int)((y / 2.24 + .5) * size);
            if (cx < 4 || cy < 4 || cx >= size - 4 || cy >= size - 4) continue;
            var light = .009 + Math.Pow(random.NextDouble(), 8) * .16;
            var warm = r < .19 || random.NextDouble() < .40;
            var tintR = warm ? 1.0 : .57;
            var tintG = warm ? .80 : .78;
            var tintB = warm ? .55 : 1.0;
            for (var oy = -2; oy <= 2; oy++)
                for (var ox = -2; ox <= 2; ox++)
                {
                    var i = (cy + oy) * size + cx + ox;
                    var a = light * Math.Exp(-(ox * ox + oy * oy) * 1.6) * transmission[i] / 255;
                    red[i] += (float)(a * tintR); green[i] += (float)(a * tintG); blue[i] += (float)(a * tintB);
                }
        }
        var pixels = new byte[size * size * 4];
        for (var i = 0; i < red.Length; i++)
        {
            if ((i & 65535) == 0) cancellation.ThrowIfCancellationRequested();
            byte Tone(float c) => (byte)Math.Clamp(255 * Math.Pow(1 - Math.Exp(-Math.Max(0, c) * 1.8), .56), 0, 255);
            var r = Tone(red[i]); var g = Tone(green[i]); var b = Tone(blue[i]);
            pixels[i * 4] = b; pixels[i * 4 + 1] = g; pixels[i * 4 + 2] = r;
            pixels[i * 4 + 3] = Math.Max(r, Math.Max(g, b));
        }
        var bitmap = BitmapSource.Create(size, size, 96, 96, PixelFormats.Pbgra32, null, pixels, size * 4);
        bitmap.Freeze();
        return bitmap;
    }
    private static double Normal(Random random) => Math.Sqrt(-2 * Math.Log(Math.Max(1e-12, random.NextDouble()))) * Math.Cos(Math.Tau * random.NextDouble());
    private static double Smooth(double a, double b, double x) { var t = Math.Clamp((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t); }
    private static double Fbm(double x, double y, uint seed) => Noise(x, y, seed) * .57 + Noise(x * 2.03, y * 2.03, seed + 1) * .28 + Noise(x * 4.11, y * 4.11, seed + 2) * .15;
    private static double Noise(double x, double y, uint seed)
    {
        var ix = (int)Math.Floor(x); var iy = (int)Math.Floor(y);
        var tx = x - ix; var ty = y - iy; tx = tx * tx * (3 - 2 * tx); ty = ty * ty * (3 - 2 * ty);
        var a = Hash(ix, iy, seed); var b = Hash(ix + 1, iy, seed); var c = Hash(ix, iy + 1, seed); var d = Hash(ix + 1, iy + 1, seed);
        return (a + (b - a) * tx) * (1 - ty) + (c + (d - c) * tx) * ty;
    }
    private static double Hash(int x, int y, uint seed)
    {
        unchecked
        {
            var h = (uint)x * 0x8da6b343u ^ (uint)y * 0xd8163841u ^ seed;
            h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16;
            return (h & 0xFFFFFF) / 16777215.0;
        }
    }
}
