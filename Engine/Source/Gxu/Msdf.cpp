#include "Engine/Source/Gxu/Msdf.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include <freetype/ftoutln.h>
#include "Engine/Source/Gxu/GxuFontFont.h"
#include "Engine/Source/Gxu/IGxuFontGlyph.h"
#include "Engine/Source/Storm/H/storm.h"

// ʕ •ᴥ•ʔ✿ Multi-channel signed distance, after Chlumsky. Edge distance is negative inside a CCW contour ✿ ʕ •ᴥ•ʔ

namespace {

enum EdgeColor {
    kBlack = 0,
    kRed = 1,
    kGreen = 2,
    kYellow = 3,
    kBlue = 4,
    kMagenta = 5,
    kCyan = 6,
    kWhite = 7,
};

struct Vec2 {
    double x;
    double y;

    Vec2 operator+(Vec2 other) const { return { x + other.x, y + other.y }; }
    Vec2 operator-(Vec2 other) const { return { x - other.x, y - other.y }; }
    Vec2 operator*(double scale) const { return { x * scale, y * scale }; }
    explicit operator bool() const { return x != 0.0 || y != 0.0; }
};

double Dot(Vec2 a, Vec2 b) {
    return a.x * b.x + a.y * b.y;
}

double Cross(Vec2 a, Vec2 b) {
    return a.x * b.y - a.y * b.x;
}

double Length(Vec2 v) {
    return std::sqrt(Dot(v, v));
}

Vec2 Normalized(Vec2 v) {
    double length = Length(v);
    return length > 0.0 ? v * (1.0 / length) : Vec2{ 0.0, 0.0 };
}

Vec2 Mix(Vec2 a, Vec2 b, double t) {
    return a + (b - a) * t;
}

int NonZeroSign(double value) {
    return value > 0.0 ? 1 : -1;
}

struct SignedDist {
    double distance;
    double dot;
    double param;
};

struct Edge {
    int type;
    int color;
    Vec2 p[4];

    Vec2 Point(double t) const {
        if (type == 1) {
            return Mix(p[0], p[1], t);
        }

        if (type == 2) {
            return Mix(Mix(p[0], p[1], t), Mix(p[1], p[2], t), t);
        }

        Vec2 p12 = Mix(p[1], p[2], t);
        return Mix(Mix(Mix(p[0], p[1], t), p12, t), Mix(p12, Mix(p[2], p[3], t), t), t);
    }

    Vec2 Direction(double t) const {
        if (type == 1) {
            return p[1] - p[0];
        }

        if (type == 2) {
            Vec2 tangent = Mix(p[1] - p[0], p[2] - p[1], t);
            return tangent ? tangent : p[2] - p[0];
        }

        Vec2 tangent = Mix(Mix(p[1] - p[0], p[2] - p[1], t), Mix(p[2] - p[1], p[3] - p[2], t), t);

        if (!tangent) {
            if (t == 0.0) {
                return p[2] - p[0];
            }

            if (t == 1.0) {
                return p[3] - p[1];
            }
        }

        return tangent;
    }

    Vec2 End() const {
        return p[type];
    }

    void Reverse() {
        if (type == 1) {
            std::swap(p[0], p[1]);
        } else if (type == 2) {
            std::swap(p[0], p[2]);
        } else {
            std::swap(p[0], p[3]);
            std::swap(p[1], p[2]);
        }
    }

    void Bound(double& minX, double& minY, double& maxX, double& maxY) const {
        for (int i = 0; i <= type; i++) {
            minX = std::min(minX, p[i].x);
            minY = std::min(minY, p[i].y);
            maxX = std::max(maxX, p[i].x);
            maxY = std::max(maxY, p[i].y);
        }
    }
};

int SolveQuadratic(double x[2], double a, double b, double c) {
    if (a == 0.0 || std::fabs(b) > 1e12 * std::fabs(a)) {
        if (b == 0.0) {
            return c == 0.0 ? -1 : 0;
        }

        x[0] = -c / b;
        return 1;
    }

    double discriminant = b * b - 4.0 * a * c;

    if (discriminant > 0.0) {
        discriminant = std::sqrt(discriminant);
        x[0] = (-b + discriminant) / (2.0 * a);
        x[1] = (-b - discriminant) / (2.0 * a);
        return 2;
    }

    if (discriminant == 0.0) {
        x[0] = -b / (2.0 * a);
        return 1;
    }

    return 0;
}

int SolveCubicNormed(double x[3], double a, double b, double c) {
    double a2 = a * a;
    double q = (1.0 / 9.0) * (a2 - 3.0 * b);
    double r = (1.0 / 54.0) * (a * (2.0 * a2 - 9.0 * b) + 27.0 * c);
    double r2 = r * r;
    double q3 = q * q * q;
    a *= 1.0 / 3.0;

    if (r2 < q3) {
        double t = r / std::sqrt(q3);
        t = std::clamp(t, -1.0, 1.0);
        t = std::acos(t);
        q = -2.0 * std::sqrt(q);
        x[0] = q * std::cos(t / 3.0) - a;
        x[1] = q * std::cos((t + 6.283185307179586) / 3.0) - a;
        x[2] = q * std::cos((t - 6.283185307179586) / 3.0) - a;
        return 3;
    }

    double u = (r < 0.0 ? 1.0 : -1.0) * std::pow(std::fabs(r) + std::sqrt(r2 - q3), 1.0 / 3.0);
    double v = u == 0.0 ? 0.0 : q / u;
    x[0] = (u + v) - a;

    if (u == v || std::fabs(u - v) < 1e-12 * std::fabs(u + v)) {
        x[1] = -0.5 * (u + v) - a;
        return 2;
    }

    return 1;
}

int SolveCubic(double x[3], double a, double b, double c, double d) {
    if (a != 0.0) {
        double bn = b / a;

        if (std::fabs(bn) < 1e6) {
            return SolveCubicNormed(x, bn, c / a, d / a);
        }
    }

    return SolveQuadratic(x, b, c, d);
}

void ToPerpendicular(const Edge& edge, SignedDist& distance, Vec2 origin, double param) {
    if (param < 0.0) {
        Vec2 dir = Normalized(edge.Direction(0.0));
        Vec2 aq = origin - edge.p[0];
        double ts = Dot(aq, dir);

        if (ts < 0.0) {
            double perpendicular = Cross(aq, dir);

            if (std::fabs(perpendicular) <= std::fabs(distance.distance)) {
                distance.distance = perpendicular;
                distance.dot = 0.0;
            }
        }
    } else if (param > 1.0) {
        Vec2 dir = Normalized(edge.Direction(1.0));
        Vec2 bq = origin - edge.End();
        double ts = Dot(bq, dir);

        if (ts > 0.0) {
            double perpendicular = Cross(bq, dir);

            if (std::fabs(perpendicular) <= std::fabs(distance.distance)) {
                distance.distance = perpendicular;
                distance.dot = 0.0;
            }
        }
    }
}

SignedDist LineDistance(const Edge& edge, Vec2 origin, double& param) {
    Vec2 aq = origin - edge.p[0];
    Vec2 ab = edge.p[1] - edge.p[0];
    double abLen2 = Dot(ab, ab);

    if (abLen2 == 0.0) {
        param = 0.0;
        return { Length(aq), 1.0, 0.0 };
    }

    param = Dot(aq, ab) / abLen2;
    Vec2 eq = (param > 0.5 ? edge.p[1] : edge.p[0]) - origin;
    double endpoint = Length(eq);

    if (param > 0.0 && param < 1.0) {
        double ortho = Dot(Vec2{ ab.y, -ab.x } * (1.0 / std::sqrt(abLen2)), aq);

        if (std::fabs(ortho) < endpoint) {
            return { ortho, 0.0, param };
        }
    }

    Vec2 abn = Normalized(ab);
    Vec2 eqn = endpoint > 0.0 ? eq * (1.0 / endpoint) : Vec2{ 0.0, 0.0 };
    return { NonZeroSign(Cross(aq, ab)) * endpoint, std::fabs(Dot(abn, eqn)), param };
}

SignedDist QuadraticDistance(const Edge& edge, Vec2 origin, double& param) {
    Vec2 qa = edge.p[0] - origin;
    Vec2 ab = edge.p[1] - edge.p[0];
    Vec2 br = edge.p[2] - edge.p[1] - ab;
    double a = Dot(br, br);
    double b = 3.0 * Dot(ab, br);
    double c = 2.0 * Dot(ab, ab) + Dot(qa, br);
    double d = Dot(qa, ab);
    double roots[3];
    int solutions = SolveCubic(roots, a, b, c, d);

    Vec2 epDir = edge.Direction(0.0);
    double epLen2 = Dot(epDir, epDir);
    double minDistance = NonZeroSign(Cross(epDir, qa)) * Length(qa);
    param = epLen2 > 0.0 ? -Dot(qa, epDir) / epLen2 : 0.0;

    epDir = edge.Direction(1.0);
    double distance = Length(edge.p[2] - origin);

    if (distance < std::fabs(minDistance)) {
        minDistance = NonZeroSign(Cross(epDir, edge.p[2] - origin)) * distance;
        double dirLen2 = Dot(epDir, epDir);
        param = dirLen2 > 0.0 ? Dot(origin - edge.p[1], epDir) / dirLen2 : 1.0;
    }

    for (int i = 0; i < solutions; i++) {
        double t = roots[i];

        if (t > 0.0 && t < 1.0) {
            Vec2 qe = qa + ab * (2.0 * t) + br * (t * t);
            distance = Length(qe);

            if (distance <= std::fabs(minDistance)) {
                minDistance = NonZeroSign(Cross(ab + br * t, qe)) * distance;
                param = t;
            }
        }
    }

    if (param >= 0.0 && param <= 1.0) {
        return { minDistance, 0.0, param };
    }

    if (param < 0.5) {
        Vec2 dir = Normalized(edge.Direction(0.0));
        Vec2 qn = Length(qa) > 0.0 ? qa * (1.0 / Length(qa)) : Vec2{ 0.0, 0.0 };
        return { minDistance, std::fabs(Dot(dir, qn)), param };
    }

    Vec2 dir = Normalized(edge.Direction(1.0));
    Vec2 bq = edge.p[2] - origin;
    Vec2 bn = Length(bq) > 0.0 ? bq * (1.0 / Length(bq)) : Vec2{ 0.0, 0.0 };
    return { minDistance, std::fabs(Dot(dir, bn)), param };
}

SignedDist CubicDistance(const Edge& edge, Vec2 origin, double& param) {
    Vec2 qa = edge.p[0] - origin;
    Vec2 ab = edge.p[1] - edge.p[0];
    Vec2 br = edge.p[2] - edge.p[1] - ab;
    Vec2 as = (edge.p[3] - edge.p[2]) - (edge.p[2] - edge.p[1]) - br;

    Vec2 epDir = edge.Direction(0.0);
    double epLen2 = Dot(epDir, epDir);
    double minDistance = NonZeroSign(Cross(epDir, qa)) * Length(qa);
    param = epLen2 > 0.0 ? -Dot(qa, epDir) / epLen2 : 0.0;

    epDir = edge.Direction(1.0);
    double distance = Length(edge.p[3] - origin);

    if (distance < std::fabs(minDistance)) {
        minDistance = NonZeroSign(Cross(epDir, edge.p[3] - origin)) * distance;
        double dirLen2 = Dot(epDir, epDir);
        param = dirLen2 > 0.0 ? Dot(epDir - (edge.p[3] - origin), epDir) / dirLen2 : 1.0;
    }

    for (int i = 0; i <= 4; i++) {
        double t = i / 4.0;
        Vec2 qe = qa + ab * (3.0 * t) + br * (3.0 * t * t) + as * (t * t * t);

        for (int step = 0; step < 4; step++) {
            Vec2 d1 = ab * 3.0 + br * (6.0 * t) + as * (3.0 * t * t);
            Vec2 d2 = br * 6.0 + as * (6.0 * t);
            double denom = Dot(d1, d1) + Dot(qe, d2);

            if (denom != 0.0) {
                t -= Dot(qe, d1) / denom;
            }

            if (t <= 0.0 || t >= 1.0) {
                break;
            }

            qe = qa + ab * (3.0 * t) + br * (3.0 * t * t) + as * (t * t * t);
            distance = Length(qe);

            if (distance < std::fabs(minDistance)) {
                minDistance = NonZeroSign(Cross(d1, qe)) * distance;
                param = t;
            }
        }
    }

    if (param >= 0.0 && param <= 1.0) {
        return { minDistance, 0.0, param };
    }

    if (param < 0.5) {
        Vec2 dir = Normalized(edge.Direction(0.0));
        double qaLen = Length(qa);
        Vec2 qn = qaLen > 0.0 ? qa * (1.0 / qaLen) : Vec2{ 0.0, 0.0 };
        return { minDistance, std::fabs(Dot(dir, qn)), param };
    }

    Vec2 dir = Normalized(edge.Direction(1.0));
    Vec2 bq = edge.p[3] - origin;
    double bqLen = Length(bq);
    Vec2 bn = bqLen > 0.0 ? bq * (1.0 / bqLen) : Vec2{ 0.0, 0.0 };
    return { minDistance, std::fabs(Dot(dir, bn)), param };
}

SignedDist EdgeDistance(const Edge& edge, Vec2 origin) {
    double param = 0.0;
    SignedDist distance;

    if (edge.type == 1) {
        distance = LineDistance(edge, origin, param);
    } else if (edge.type == 2) {
        distance = QuadraticDistance(edge, origin, param);
    } else {
        distance = CubicDistance(edge, origin, param);
    }

    distance.param = param;
    return distance;
}

Edge MakeLine(Vec2 a, Vec2 b) {
    Edge edge{};
    edge.type = 1;
    edge.color = kWhite;
    edge.p[0] = a;
    edge.p[1] = b;
    return edge;
}

Edge MakeQuadratic(Vec2 a, Vec2 b, Vec2 c) {
    if (Cross(b - a, c - b) == 0.0) {
        return MakeLine(a, c);
    }

    Edge edge{};
    edge.type = 2;
    edge.color = kWhite;
    edge.p[0] = a;
    edge.p[1] = b;
    edge.p[2] = c;
    return edge;
}

Edge MakeCubic(Vec2 a, Vec2 b, Vec2 c, Vec2 d) {
    Vec2 p12 = c - b;

    if (Cross(b - a, p12) == 0.0 && Cross(p12, d - c) == 0.0) {
        return MakeLine(a, d);
    }

    Vec2 quad = b * 1.5 - a * 0.5;

    if (quad.x == (c * 1.5 - d * 0.5).x && quad.y == (c * 1.5 - d * 0.5).y) {
        return MakeQuadratic(a, quad, d);
    }

    Edge edge{};
    edge.type = 3;
    edge.color = kWhite;
    edge.p[0] = a;
    edge.p[1] = b;
    edge.p[2] = c;
    edge.p[3] = d;
    return edge;
}

void SplitThirds(const Edge& edge, Edge out[3]) {
    if (edge.type == 1) {
        out[0] = MakeLine(edge.p[0], edge.Point(1.0 / 3.0));
        out[1] = MakeLine(edge.Point(1.0 / 3.0), edge.Point(2.0 / 3.0));
        out[2] = MakeLine(edge.Point(2.0 / 3.0), edge.p[1]);
    } else if (edge.type == 2) {
        out[0] = MakeQuadratic(edge.p[0], Mix(edge.p[0], edge.p[1], 1.0 / 3.0), edge.Point(1.0 / 3.0));
        out[1] = MakeQuadratic(edge.Point(1.0 / 3.0), Mix(Mix(edge.p[0], edge.p[1], 5.0 / 9.0), Mix(edge.p[1], edge.p[2], 4.0 / 9.0), 0.5), edge.Point(2.0 / 3.0));
        out[2] = MakeQuadratic(edge.Point(2.0 / 3.0), Mix(edge.p[1], edge.p[2], 2.0 / 3.0), edge.p[2]);
    } else {
        Vec2 a = edge.p[0];
        Vec2 b = edge.p[1];
        Vec2 c = edge.p[2];
        Vec2 d = edge.p[3];
        out[0] = MakeCubic(a, a.x == b.x && a.y == b.y ? a : Mix(a, b, 1.0 / 3.0), Mix(Mix(a, b, 1.0 / 3.0), Mix(b, c, 1.0 / 3.0), 1.0 / 3.0), edge.Point(1.0 / 3.0));
        out[1] = MakeCubic(
            edge.Point(1.0 / 3.0),
            Mix(Mix(Mix(a, b, 1.0 / 3.0), Mix(b, c, 1.0 / 3.0), 1.0 / 3.0), Mix(Mix(b, c, 1.0 / 3.0), Mix(c, d, 1.0 / 3.0), 1.0 / 3.0), 2.0 / 3.0),
            Mix(Mix(Mix(a, b, 2.0 / 3.0), Mix(b, c, 2.0 / 3.0), 2.0 / 3.0), Mix(Mix(b, c, 2.0 / 3.0), Mix(c, d, 2.0 / 3.0), 2.0 / 3.0), 1.0 / 3.0),
            edge.Point(2.0 / 3.0));
        out[2] = MakeCubic(edge.Point(2.0 / 3.0), Mix(Mix(b, c, 2.0 / 3.0), Mix(c, d, 2.0 / 3.0), 2.0 / 3.0), c.x == d.x && c.y == d.y ? d : Mix(c, d, 2.0 / 3.0), d);
    }

    out[0].color = edge.color;
    out[1].color = edge.color;
    out[2].color = edge.color;
}

int Seed2(uint64_t& seed) {
    int value = static_cast<int>(seed) & 1;
    seed >>= 1;
    return value;
}

int Seed3(uint64_t& seed) {
    int value = static_cast<int>(seed % 3);
    seed /= 3;
    return value;
}

int InitColor(uint64_t& seed) {
    static const int colors[3] = { kCyan, kMagenta, kYellow };
    return colors[Seed3(seed)];
}

void SwitchColor(int& color, uint64_t& seed) {
    int shifted = color << (1 + Seed2(seed));
    color = (shifted | (shifted >> 3)) & kWhite;
}

void SwitchColor(int& color, uint64_t& seed, int banned) {
    int combined = color & banned;

    if (combined == kRed || combined == kGreen || combined == kBlue) {
        color = combined ^ kWhite;
    } else {
        SwitchColor(color, seed);
    }
}

bool IsCorner(Vec2 aDir, Vec2 bDir, double crossThreshold) {
    aDir = Normalized(aDir);
    bDir = Normalized(bDir);
    return Dot(aDir, bDir) <= 0.0 || std::fabs(Cross(aDir, bDir)) > crossThreshold;
}

int SymmetricalTrichotomy(int position, int count) {
    return static_cast<int>(3.0 + 2.875 * position / (count - 1) - 1.4375 + 0.5) - 3;
}

void ColorContour(std::vector<Edge>& edges, int& color, uint64_t& seed) {
    if (edges.empty()) {
        return;
    }

    double crossThreshold = std::sin(3.0);
    std::vector<int> corners;
    Vec2 prev = edges.back().Direction(1.0);

    for (int index = 0; index < static_cast<int>(edges.size()); index++) {
        if (IsCorner(prev, edges[index].Direction(0.0), crossThreshold)) {
            corners.push_back(index);
        }

        prev = edges[index].Direction(1.0);
    }

    if (corners.empty()) {
        SwitchColor(color, seed);

        for (Edge& edge : edges) {
            edge.color = color;
        }

        return;
    }

    if (corners.size() == 1) {
        int colors[3];
        SwitchColor(color, seed);
        colors[0] = color;
        colors[1] = kWhite;
        SwitchColor(color, seed);
        colors[2] = color;
        int corner = corners[0];
        int count = static_cast<int>(edges.size());

        if (count >= 3) {
            for (int i = 0; i < count; i++) {
                edges[(corner + i) % count].color = colors[1 + SymmetricalTrichotomy(i, count)];
            }

            return;
        }

        Edge parts[6]{};
        Edge first[3];
        SplitThirds(edges[0], first);

        if (count >= 2) {
            Edge second[3];
            SplitThirds(edges[1], second);

            for (int i = 0; i < 3; i++) {
                parts[i + 3 * corner] = first[i];
                parts[i + 3 - 3 * corner] = second[i];
            }

            parts[0].color = parts[1].color = colors[0];
            parts[2].color = parts[3].color = colors[1];
            parts[4].color = parts[5].color = colors[2];
            edges.clear();

            for (int i = 0; i < 6; i++) {
                edges.push_back(parts[i]);
            }
        } else {
            first[0].color = colors[0];
            first[1].color = colors[1];
            first[2].color = colors[2];
            edges.assign(first, first + 3);
        }

        return;
    }

    int cornerCount = static_cast<int>(corners.size());
    int spline = 0;
    int start = corners[0];
    int count = static_cast<int>(edges.size());
    SwitchColor(color, seed);
    int initial = color;

    for (int i = 0; i < count; i++) {
        int index = (start + i) % count;

        if (spline + 1 < cornerCount && corners[spline + 1] == index) {
            spline++;
            SwitchColor(color, seed, (spline == cornerCount - 1) ? initial : kBlack);
        }

        edges[index].color = color;
    }
}

int Winding(const std::vector<std::vector<Edge>>& contours, Vec2 probe) {
    int winding = 0;

    for (const std::vector<Edge>& contour : contours) {
        for (const Edge& edge : contour) {
            Vec2 prev = edge.p[0];

            for (int step = 1; step <= 8; step++) {
                Vec2 next = edge.Point(step / 8.0);
                bool up = prev.y <= probe.y && next.y > probe.y;
                bool down = next.y <= probe.y && prev.y > probe.y;

                if (up || down) {
                    double x = prev.x + (probe.y - prev.y) / (next.y - prev.y) * (next.x - prev.x);

                    if (x > probe.x) {
                        winding += up ? 1 : -1;
                    }
                }

                prev = next;
            }
        }
    }

    return winding;
}

bool ContourNeedsReverse(const std::vector<Edge>& contour, const std::vector<std::vector<Edge>>& contours) {
    if (contour.empty()) {
        return false;
    }

    const Edge& edge = contour.front();
    Vec2 dir = Normalized(edge.Direction(0.5));

    if (!dir) {
        return false;
    }

    // ʕ •ᴥ•ʔ✿ Ink has to sit on the left of every contour, including holes ✿ ʕ •ᴥ•ʔ
    Vec2 probe = edge.Point(0.5) + Vec2{ -dir.y, dir.x } * 0.05;
    return Winding(contours, probe) == 0;
}

struct Builder {
    std::vector<std::vector<Edge>> contours;
    double x = 0.0;
    double y = 0.0;
    double startX = 0.0;
    double startY = 0.0;
    bool open = false;

    void Close() {
        if (!open || contours.empty()) {
            open = false;
            return;
        }

        if (x != startX || y != startY) {
            contours.back().push_back(MakeLine({ x, y }, { startX, startY }));
        }

        open = false;
    }

    void Move(double toX, double toY) {
        Close();
        contours.emplace_back();
        x = startX = toX;
        y = startY = toY;
        open = true;
    }

    void Line(double toX, double toY) {
        if (!open || (toX == x && toY == y)) {
            x = toX;
            y = toY;
            return;
        }

        contours.back().push_back(MakeLine({ x, y }, { toX, toY }));
        x = toX;
        y = toY;
    }

    void Conic(double cx, double cy, double toX, double toY) {
        if (!open) {
            return;
        }

        contours.back().push_back(MakeQuadratic({ x, y }, { cx, cy }, { toX, toY }));
        x = toX;
        y = toY;
    }

    void Cubic(double c1x, double c1y, double c2x, double c2y, double toX, double toY) {
        if (!open) {
            return;
        }

        contours.back().push_back(MakeCubic({ x, y }, { c1x, c1y }, { c2x, c2y }, { toX, toY }));
        x = toX;
        y = toY;
    }
};

double Coord(FT_Pos pos) {
    return static_cast<double>(pos) / 64.0;
}

int MoveTo(FT_Vector* to, void* user) {
    static_cast<Builder*>(user)->Move(Coord(to->x), Coord(to->y));
    return 0;
}

int LineTo(FT_Vector* to, void* user) {
    static_cast<Builder*>(user)->Line(Coord(to->x), Coord(to->y));
    return 0;
}

int ConicTo(FT_Vector* control, FT_Vector* to, void* user) {
    static_cast<Builder*>(user)->Conic(Coord(control->x), Coord(control->y), Coord(to->x), Coord(to->y));
    return 0;
}

int CubicTo(FT_Vector* control1, FT_Vector* control2, FT_Vector* to, void* user) {
    static_cast<Builder*>(user)->Cubic(Coord(control1->x), Coord(control1->y), Coord(control2->x), Coord(control2->y), Coord(to->x), Coord(to->y));
    return 0;
}

struct Channel {
    double distance = 1e24;
    double dot = 1.0;
    double param = 0.0;
    const Edge* edge = nullptr;
};

void Consider(Channel& channel, const SignedDist& sample, const Edge& edge) {
    double next = std::fabs(sample.distance);
    double best = std::fabs(channel.distance);

    if (next < best || (std::fabs(next - best) <= 1e-9 && sample.dot < channel.dot)) {
        channel.distance = sample.distance;
        channel.dot = sample.dot;
        channel.param = sample.param;
        channel.edge = &edge;
    }
}

void FinishChannel(Channel& channel, Vec2 origin) {
    if (!channel.edge) {
        return;
    }

    SignedDist sample{ channel.distance, channel.dot, channel.param };
    ToPerpendicular(*channel.edge, sample, origin, channel.param);
    channel.distance = sample.distance;
}

double Median(double a, double b, double c) {
    return std::max(std::min(a, b), std::min(std::max(a, b), c));
}

float FieldValue(double distance) {
    // ʕ •ᴥ•ʔ✿ Raw distance is negative inside. The shader treats values above 0.5 as ink ✿ ʕ •ᴥ•ʔ
    double value = (-distance) / static_cast<double>(GxuFontMsdfRange()) + 0.5;
    return static_cast<float>(std::clamp(value, 0.0, 1.0));
}

bool MedianOutside(float am, float bm, float xm) {
    return xm < std::min(am, bm) || xm > std::max(am, bm);
}

bool DeviationExceeded(float am, float bm, float xm, double t, float span) {
    float ax = static_cast<float>(t) * span;
    float bx = (1.0f - static_cast<float>(t)) * span;
    return xm < am - ax || xm > am + ax || xm < bm - bx || xm > bm + bx;
}

// ʕ •ᴥ•ʔ✿ A channel pair meeting between two texels is where the filtered median spikes ✿ ʕ •ᴥ•ʔ
bool LinearArtifact(const float a[3], const float b[3], float span) {
    float am = static_cast<float>(Median(a[0], a[1], a[2]));
    float bm = static_cast<float>(Median(b[0], b[1], b[2]));

    if (std::fabs(am - 0.5f) < std::fabs(bm - 0.5f)) {
        return false;
    }

    float deltaA[3] = { a[1] - a[0], a[2] - a[1], a[0] - a[2] };
    float deltaB[3] = { b[1] - b[0], b[2] - b[1], b[0] - b[2] };

    for (int pair = 0; pair < 3; pair++) {
        float denom = deltaA[pair] - deltaB[pair];

        if (denom == 0.0f) {
            continue;
        }

        double t = static_cast<double>(deltaA[pair]) / denom;

        if (t <= 0.01 || t >= 0.99) {
            continue;
        }

        float mixed[3] = {
            static_cast<float>(a[0] + (b[0] - a[0]) * t),
            static_cast<float>(a[1] + (b[1] - a[1]) * t),
            static_cast<float>(a[2] + (b[2] - a[2]) * t),
        };
        float xm = static_cast<float>(Median(mixed[0], mixed[1], mixed[2]));

        if (MedianOutside(am, bm, xm) && DeviationExceeded(am, bm, xm, t, span)) {
            return true;
        }
    }

    return false;
}

bool DiagonalArtifact(const float a[3], const float b[3], const float c[3], const float d[3], float span) {
    float am = static_cast<float>(Median(a[0], a[1], a[2]));
    float dm = static_cast<float>(Median(d[0], d[1], d[2]));

    if (std::fabs(am - 0.5f) < std::fabs(dm - 0.5f)) {
        return false;
    }

    float linear[3];
    float quad[3];
    double extreme[3];

    for (int channel = 0; channel < 3; channel++) {
        float abc = a[channel] - b[channel] - c[channel];
        linear[channel] = -a[channel] - abc;
        quad[channel] = d[channel] + abc;
        extreme[channel] = quad[channel] != 0.0f ? -0.5 * linear[channel] / quad[channel] : 2.0;
    }

    float deltaA[3] = { a[1] - a[0], a[2] - a[1], a[0] - a[2] };
    float deltaD[3] = { d[1] - d[0], d[2] - d[1], d[0] - d[2] };
    float deltaBC[3] = {
        (b[1] - b[0]) + (c[1] - c[0]),
        (b[2] - b[1]) + (c[2] - c[1]),
        (b[0] - b[2]) + (c[0] - c[2]),
    };
    int extremePair[3][2] = { { 0, 1 }, { 1, 2 }, { 2, 0 } };

    for (int pair = 0; pair < 3; pair++) {
        double roots[2];
        int count = SolveQuadratic(roots, deltaD[pair] - deltaBC[pair] + deltaA[pair], deltaBC[pair] - deltaA[pair] - deltaA[pair], deltaA[pair]);

        for (int root = 0; root < count; root++) {
            double t = roots[root];

            if (t <= 0.01 || t >= 0.99) {
                continue;
            }

            auto sample = [&](double s) {
                float mixed[3];

                for (int channel = 0; channel < 3; channel++) {
                    mixed[channel] = static_cast<float>(s * (s * quad[channel] + linear[channel]) + a[channel]);
                }

                return static_cast<float>(Median(mixed[0], mixed[1], mixed[2]));
            };

            float xm = sample(t);
            bool artifact = MedianOutside(am, dm, xm) && DeviationExceeded(am, dm, xm, t, span);

            for (int end = 0; end < 2 && !artifact; end++) {
                double tEx = extreme[extremePair[pair][end]];

                if (tEx <= 0.0 || tEx >= 1.0) {
                    continue;
                }

                float bound0 = am;
                float bound1 = dm;
                double from = 0.0;
                double to = 1.0;

                if (tEx > t) {
                    bound1 = sample(tEx);
                    to = tEx;
                } else {
                    bound0 = sample(tEx);
                    from = tEx;
                }

                float ax = static_cast<float>(t - from) * span;
                float bx = static_cast<float>(to - t) * span;
                artifact = MedianOutside(bound0, bound1, xm) && (xm < bound0 - ax || xm > bound0 + ax || xm < bound1 - bx || xm > bound1 + bx);
            }

            if (artifact) {
                return true;
            }
        }
    }

    return false;
}

// ʕ •ᴥ•ʔ✿ Filtering two agreeing texels can still paint a ring just outside the stroke ✿ ʕ •ᴥ•ʔ
void FlattenArtifacts(float* field, int width, int height) {
    float span = 1.1111111111111111f / GxuFontMsdfRange();
    float diagonal = span * 1.41421356f;
    std::vector<char> error(static_cast<size_t>(width) * height);

    auto at = [&](int x, int y) {
        return field + (static_cast<size_t>(y) * width + x) * 3;
    };

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            float* texel = at(x, y);
            bool bad = false;

            if (x > 0) {
                bad = LinearArtifact(texel, at(x - 1, y), span);
            }

            if (!bad && x + 1 < width) {
                bad = LinearArtifact(texel, at(x + 1, y), span);
            }

            if (!bad && y > 0) {
                bad = LinearArtifact(texel, at(x, y - 1), span);
            }

            if (!bad && y + 1 < height) {
                bad = LinearArtifact(texel, at(x, y + 1), span);
            }

            if (!bad && x > 0 && y > 0) {
                bad = DiagonalArtifact(texel, at(x - 1, y), at(x, y - 1), at(x - 1, y - 1), diagonal);
            }

            if (!bad && x + 1 < width && y > 0) {
                bad = DiagonalArtifact(texel, at(x + 1, y), at(x, y - 1), at(x + 1, y - 1), diagonal);
            }

            if (!bad && x > 0 && y + 1 < height) {
                bad = DiagonalArtifact(texel, at(x - 1, y), at(x, y + 1), at(x - 1, y + 1), diagonal);
            }

            if (!bad && x + 1 < width && y + 1 < height) {
                bad = DiagonalArtifact(texel, at(x + 1, y), at(x, y + 1), at(x + 1, y + 1), diagonal);
            }

            error[static_cast<size_t>(y) * width + x] = bad;
        }
    }

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            if (!error[static_cast<size_t>(y) * width + x]) {
                continue;
            }

            float* texel = at(x, y);
            float median = static_cast<float>(Median(texel[0], texel[1], texel[2]));
            texel[0] = median;
            texel[1] = median;
            texel[2] = median;
        }
    }
}

}

bool MsdfRasterizeGlyph(FT_Face face, uint32_t pixelSize, uint32_t code, uint32_t baseline, uint32_t cellHeight, MsdfGlyph* glyph) {
    if (!face || !glyph || !pixelSize || !cellHeight) {
        return false;
    }

    FT_UInt index = FT_Get_Char_Index(face, code);

    if (!index || FT_Load_Glyph(face, index, FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING | FT_LOAD_PEDANTIC | FT_LOAD_IGNORE_GLOBAL_ADVANCE_WIDTH)) {
        return false;
    }

    glyph->m_advance = static_cast<float>(GxuFontGlyphAdvancePixels(face, code, pixelSize));
    glyph->m_rgba = nullptr;
    glyph->m_size = 0;
    glyph->m_width = 0;
    glyph->m_height = 0;
    glyph->m_pitch = 0;
    glyph->m_bearing = 0.0f;

    Builder builder;
    FT_Outline_Funcs funcs = { MoveTo, LineTo, ConicTo, CubicTo, 0, 0 };

    if (face->glyph->outline.n_points > 0) {
        if (FT_Outline_Decompose(&face->glyph->outline, &funcs, &builder)) {
            return false;
        }

        builder.Close();
    }

    uint64_t seed = 0;
    int color = InitColor(seed);
    std::vector<Edge> edges;

    std::vector<char> reverseContour(builder.contours.size());

    for (size_t i = 0; i < builder.contours.size(); i++) {
        reverseContour[i] = ContourNeedsReverse(builder.contours[i], builder.contours);
    }

    for (size_t i = 0; i < builder.contours.size(); i++) {
        std::vector<Edge>& contour = builder.contours[i];

        if (reverseContour[i]) {
            std::reverse(contour.begin(), contour.end());

            for (Edge& edge : contour) {
                edge.Reverse();
            }
        }

        ColorContour(contour, color, seed);
        edges.insert(edges.end(), contour.begin(), contour.end());
    }

    double range = static_cast<double>(GxuFontMsdfRange());
    int left = 0;
    int right = 1;

    if (!edges.empty()) {
        double minX = 1e24;
        double minY = 1e24;
        double maxX = -1e24;
        double maxY = -1e24;

        for (const Edge& edge : edges) {
            edge.Bound(minX, minY, maxX, maxY);
        }

        left = static_cast<int>(std::floor(minX)) - static_cast<int>(range);
        right = static_cast<int>(std::ceil(maxX)) + static_cast<int>(range);
    }

    if (right <= left) {
        right = left + 1;
    }

    int width = right - left;

    if (width > 256) {
        width = 256;
    }

    uint32_t height = cellHeight;
    uint32_t pitch = static_cast<uint32_t>(width) * 4;
    uint32_t size = pitch * height;
    uint32_t* pixels = static_cast<uint32_t*>(SMemAlloc(size, __FILE__, __LINE__, 0));

    if (!pixels) {
        return false;
    }

    std::fill(pixels, pixels + size / 4, 0u);

    std::vector<float> field(static_cast<size_t>(width) * height * 3);
    double baselineRow = static_cast<double>(baseline) + range;

    for (uint32_t row = 0; row < height; row++) {
        for (int col = 0; col < width; col++) {
            Vec2 origin{ static_cast<double>(left + col) + 0.5, baselineRow - static_cast<double>(row) - 0.5 };
            Channel red;
            Channel green;
            Channel blue;
            Channel nearest;

            for (const Edge& edge : edges) {
                if (Length(edge.End() - edge.p[0]) == 0.0 && edge.type == 1) {
                    continue;
                }

                SignedDist sample = EdgeDistance(edge, origin);
                Consider(nearest, sample, edge);

                if (edge.color & kRed) {
                    Consider(red, sample, edge);
                }

                if (edge.color & kGreen) {
                    Consider(green, sample, edge);
                }

                if (edge.color & kBlue) {
                    Consider(blue, sample, edge);
                }
            }

            FinishChannel(red, origin);
            FinishChannel(green, origin);
            FinishChannel(blue, origin);
            FinishChannel(nearest, origin);

            double med = Median(red.distance, green.distance, blue.distance);

            if (med * nearest.distance < 0.0) {
                red.distance = green.distance = blue.distance = nearest.distance;
            }

            float* texel = &field[(static_cast<size_t>(row) * width + col) * 3];
            texel[0] = FieldValue(red.distance);
            texel[1] = FieldValue(green.distance);
            texel[2] = FieldValue(blue.distance);
        }
    }

    FlattenArtifacts(field.data(), width, static_cast<int>(height));

    for (uint32_t row = 0; row < height; row++) {
        for (int col = 0; col < width; col++) {
            const float* texel = &field[(static_cast<size_t>(row) * width + col) * 3];
            uint32_t r = static_cast<uint32_t>(texel[0] * 255.0f + 0.5f);
            uint32_t g = static_cast<uint32_t>(texel[1] * 255.0f + 0.5f);
            uint32_t b = static_cast<uint32_t>(texel[2] * 255.0f + 0.5f);
            pixels[row * width + col] = 0xFF000000u | (r << 16) | (g << 8) | b;
        }
    }

    glyph->m_rgba = pixels;
    glyph->m_size = size;
    glyph->m_width = static_cast<uint32_t>(width);
    glyph->m_height = height;
    glyph->m_pitch = pitch;
    glyph->m_bearing = static_cast<float>(left);
    return true;
}

