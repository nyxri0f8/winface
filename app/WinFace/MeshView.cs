using System.Windows;
using System.Windows.Media;

namespace WinFace;

/// <summary>Live view for enrolment and tests: Face ID-style corner brackets, the 478 landmarks as dots (never camera
/// pixels) and a progress arc. Mirrored, so it moves like a mirror.</summary>
public class MeshView : FrameworkElement
{
    float[] _mesh = [];
    double _progress;
    Brush _accent = (Brush)Application.Current.Resources["Accent"];
    int _result;   // 0 running, 1 ok, -1 failed

    public void Set(float[] mesh, double progress)
    {
        _mesh = mesh;
        _progress = Math.Clamp(progress, 0, 1);
        InvalidateVisual();
    }

    public void SetResult(int result)
    {
        _result = result;
        _accent = (Brush)Application.Current.Resources[result == 1 ? "Good" : result == -1 ? "Bad" : "Accent"];
        InvalidateVisual();
    }

    public void Reset()
    {
        _mesh = [];
        _progress = 0;
        SetResult(0);
    }

    protected override void OnRender(DrawingContext dc)
    {
        double w = ActualWidth, h = ActualHeight, s = Math.Min(w, h);
        if (s <= 0) return;
        var c = new Point(w / 2, h / 2);
        dc.DrawRectangle(Brushes.Transparent, null, new Rect(0, 0, w, h));

        // progress ring
        double R = s * 0.46;
        var track = new Pen(new SolidColorBrush(Color.FromArgb(40, 255, 255, 255)), 4);
        dc.DrawEllipse(null, track, c, R, R);
        if (_progress > 0.001) DrawArc(dc, c, R, _progress, new Pen(_accent, 4) { StartLineCap = PenLineCap.Round, EndLineCap = PenLineCap.Round });

        // corner brackets
        double hb = s * 0.30, r = hb * 0.3, l = hb * 0.25;
        var white = new Pen(new SolidColorBrush(Color.FromArgb(_mesh.Length > 0 ? (byte)230 : (byte)110, 255, 255, 255)), 3.2)
        { StartLineCap = PenLineCap.Round, EndLineCap = PenLineCap.Round, LineJoin = PenLineJoin.Round };
        foreach (var (sx, sy) in new[] { (-1, -1), (1, -1), (1, 1), (-1, 1) })
        {
            var g = new StreamGeometry();
            using (var ctx = g.Open())
            {
                double ax = c.X + sx * (hb - r), ay = c.Y + sy * (hb - r);
                ctx.BeginFigure(new Point(ax - sx * l, c.Y + sy * hb), false, false);
                ctx.LineTo(new Point(ax, c.Y + sy * hb), true, false);
                bool cw = sx * sy < 0;
                ctx.ArcTo(new Point(c.X + sx * hb, ay), new Size(r, r), 0, false, cw ? SweepDirection.Clockwise : SweepDirection.Counterclockwise, true, false);
                ctx.LineTo(new Point(c.X + sx * hb, ay - sy * l), true, false);
            }
            g.Freeze();
            dc.DrawGeometry(null, white, g);
        }

        // landmarks (mirrored)
        if (_mesh.Length >= 2)
        {
            double k = s * 0.5;
            for (int i = 0; i + 1 < _mesh.Length; i += 2)
                dc.DrawEllipse(_accent, null, new Point(c.X - _mesh[i] * k, c.Y + _mesh[i + 1] * k), 1.3, 1.3);
        }
    }

    static void DrawArc(DrawingContext dc, Point c, double R, double frac, Pen pen)
    {
        if (frac >= 0.999) { dc.DrawEllipse(null, pen, c, R, R); return; }
        double a0 = -Math.PI / 2, a1 = a0 + frac * 2 * Math.PI;
        var g = new StreamGeometry();
        using (var ctx = g.Open())
        {
            ctx.BeginFigure(new Point(c.X + R * Math.Cos(a0), c.Y + R * Math.Sin(a0)), false, false);
            ctx.ArcTo(new Point(c.X + R * Math.Cos(a1), c.Y + R * Math.Sin(a1)), new Size(R, R), 0, frac > 0.5, SweepDirection.Clockwise, true, false);
        }
        g.Freeze();
        dc.DrawGeometry(null, pen, g);
    }
}
