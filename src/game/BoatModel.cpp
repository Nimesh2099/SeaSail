#include "BoatModel.h"

#include <cmath>
#include <vector>

namespace sea {

namespace {

// Palette
const Vec3 kHullPaint {0.10f, 0.27f, 0.30f};  // dark teal topsides
const Vec3 kStripe    {0.85f, 0.82f, 0.72f};  // cream rub-rail
const Vec3 kBottom    {0.42f, 0.11f, 0.07f};  // antifouling red
const Vec3 kDeck      {0.40f, 0.29f, 0.18f};  // weathered planks
const Vec3 kCabin     {0.86f, 0.84f, 0.77f};
const Vec3 kRoof      {0.72f, 0.30f, 0.11f};  // rust orange
const Vec3 kGlass     {0.06f, 0.09f, 0.11f};
const Vec3 kMetal     {0.22f, 0.22f, 0.23f};
const Vec3 kTyre      {0.05f, 0.05f, 0.05f};
const Vec3 kCrate     {0.52f, 0.38f, 0.20f};
const Vec3 kLamp      {1.00f, 0.80f, 0.45f};

// One cross-section of the hull at a given x: points from the deck edge
// down to the keel, on the starboard (+z) side. Port is the mirror image.
struct Station {
    float x;
    std::vector<Vec3> pts;  // deck edge ... keel
};

Station makeStation(float x, const BoatDimensions& d)
{
    const float L = d.bowX - d.sternX;
    const float u = (x - d.sternX) / L;          // 0 at the stern, 1 at the bow

    // Plan shape: full width aft, tapering to a point at the bow.
    float w = d.beam * 0.5f;
    if (u < 0.55f) w *= lerpf(0.86f, 1.0f, u / 0.55f);
    else           w *= std::sqrt(std::max(0.0f, 1.0f - ((u - 0.55f) / 0.45f) * ((u - 0.55f) / 0.45f)));

    // Sheer line: the deck rises toward the bow.
    float deckY = 0.85f + 0.55f * u * u;
    // Keel line: deepest amidships, rising at both ends.
    float keelY = -d.draft * (1.0f - 0.65f * std::pow(std::fabs(u - 0.45f) / 0.55f, 2.0f));
    if (u > 0.9f) keelY = lerpf(keelY, 0.4f, (u - 0.9f) / 0.1f); // the stem sweeps up

    Station s;
    s.x = x;
    s.pts = {
        {x, deckY,          w},
        {x, 0.30f,          w * 0.99f},  // rub-rail line
        {x, 0.05f,          w * 0.96f},  // boot-top: just above the waterline
        {x, keelY * 0.55f,  w * 0.78f},  // turn of the bilge
        {x, keelY,          0.0f},       // keel
    };
    return s;
}

} // namespace

void buildBoatModel(MeshBuilder& m, const BoatDimensions& d)
{
    // ---- hull --------------------------------------------------------------
    const int stations = 14;
    std::vector<Station> st;
    for (int i = 0; i <= stations; ++i)
        st.push_back(makeStation(lerpf(d.sternX, d.bowX, static_cast<float>(i) / stations), d));

    const Vec3 bandColor[4] = {kHullPaint, kStripe, kBottom, kBottom};
    for (int i = 0; i < stations; ++i) {
        const Station& a = st[i];
        const Station& b = st[i + 1];
        for (int k = 0; k + 1 < static_cast<int>(a.pts.size()); ++k) {
            Vec3 a0 = a.pts[k], a1 = a.pts[k + 1], b0 = b.pts[k], b1 = b.pts[k + 1];
            // Starboard (outward normal +z) and the mirrored port side.
            m.quad(a0, a1, b1, b0, bandColor[k]);
            auto mir = [](Vec3 v) { v.z = -v.z; return v; };
            m.quad(mir(a0), mir(b0), mir(b1), mir(a1), bandColor[k]);
        }
        // Deck between the two deck edges.
        Vec3 sa = a.pts[0], sb = b.pts[0];
        Vec3 pa{sa.x, sa.y, -sa.z}, pb{sb.x, sb.y, -sb.z};
        m.quad(pa, pb, sb, sa, kDeck);
        // Bulwark: a low wall around the deck edge.
        const float bh = 0.28f;
        m.quad(sa, sb, sb + Vec3{0, bh, 0}, sa + Vec3{0, bh, 0}, kStripe);
        m.quad(pb, pa, pa + Vec3{0, bh, 0}, pb + Vec3{0, bh, 0}, kStripe);
    }
    // Transom (flat stern): fan from the centre line out to both sides.
    {
        const Station& s = st.front();
        Vec3 centreTop{s.x, s.pts[0].y, 0.0f};
        for (int k = 0; k + 1 < static_cast<int>(s.pts.size()); ++k) {
            Vec3 p0 = s.pts[k], p1 = s.pts[k + 1];
            m.triangle(centreTop, p1, p0, kHullPaint);
            m.triangle(centreTop, Vec3{p0.x, p0.y, -p0.z}, Vec3{p1.x, p1.y, -p1.z}, kHullPaint);
        }
        Vec3 top0 = s.pts[0], top1{s.x, s.pts[0].y, -s.pts[0].z};
        m.quad(top1, top0, top0 + Vec3{0, 0.28f, 0}, top1 + Vec3{0, 0.28f, 0}, kStripe);
    }

    // ---- wheelhouse ----------------------------------------------------------
    const float deckMid = 0.85f + 0.55f * 0.45f * 0.45f; // deck height around x = 0
    const float cabinX0 = -0.9f, cabinX1 = 0.9f, cabinHalfW = 0.95f, cabinH = 1.75f;
    m.box({cabinX0, deckMid - 0.1f, -cabinHalfW}, {cabinX1, deckMid + cabinH, cabinHalfW}, kCabin);
    // Windows: dark panels standing slightly proud of the walls.
    const float wy0 = deckMid + 0.95f, wy1 = deckMid + 1.45f;
    m.box({cabinX1, wy0, -0.8f}, {cabinX1 + 0.02f, wy1, 0.8f}, kGlass);                     // front
    m.box({-0.7f, wy0, cabinHalfW}, {0.7f, wy1, cabinHalfW + 0.02f}, kGlass);              // starboard
    m.box({-0.7f, wy0, -cabinHalfW - 0.02f}, {0.7f, wy1, -cabinHalfW}, kGlass);            // port
    m.box({cabinX0 - 0.02f, wy0, -0.35f}, {cabinX0, wy1, 0.35f}, kGlass);                  // back door window
    // Roof with an overhang.
    m.box({cabinX0 - 0.15f, deckMid + cabinH, -cabinHalfW - 0.12f},
          {cabinX1 + 0.25f, deckMid + cabinH + 0.1f, cabinHalfW + 0.12f}, kRoof);

    // ---- mast, lamp, antenna -------------------------------------------------
    const float roofY = deckMid + cabinH + 0.1f;
    m.cylinder({0.2f, roofY, 0.0f}, 0.06f, 2.3f, 6, kMetal);
    m.box({0.05f, roofY + 1.6f, -0.6f}, {0.12f, roofY + 1.66f, 0.6f}, kMetal);           // cross-tree
    m.box({0.12f, roofY + 2.3f, -0.12f}, {0.36f, roofY + 2.5f, 0.12f}, kLamp, 1.0f);       // masthead lamp
    m.box({0.85f, roofY + 0.05f, -0.15f}, {1.05f, roofY + 0.25f, 0.15f}, kLamp, 1.0f);     // spotlight on the roof
    m.cylinder({-0.6f, roofY, 0.55f}, 0.02f, 1.4f, 4, kMetal);                              // whip antenna

    // ---- aft deck: crane, crates, nets ------------------------------------
    const float aftDeck = 0.85f + 0.55f * 0.05f;
    m.cylinder({-2.7f, aftDeck, 0.0f}, 0.11f, 2.1f, 8, kMetal);                           // crane post
    Quat armTilt = Quat::fromAxisAngle({0, 0, 1}, -0.45f);
    m.orientedBox({-3.35f, aftDeck + 2.0f, 0.0f}, {0.85f, 0.07f, 0.07f}, armTilt, kRoof);  // crane arm
    m.box({-3.95f, aftDeck + 0.9f, -0.02f}, {-3.91f, aftDeck + 1.75f, 0.02f}, kMetal);     // hanging line
    m.box({-2.2f, aftDeck, 0.45f}, {-1.5f, aftDeck + 0.5f, 1.05f}, kCrate);
    m.box({-2.1f, aftDeck, -1.0f}, {-1.4f, aftDeck + 0.4f, -0.4f}, kCrate);
    m.box({-1.9f, aftDeck + 0.5f, 0.55f}, {-1.55f, aftDeck + 0.8f, 0.95f}, kCrate * 0.8f);

    // ---- fenders (old tyres) along both sides ---------------------------------
    for (float x : {-2.2f, -0.4f, 1.4f}) {
        Station s = makeStation(x, d);
        float z = s.pts[1].z + 0.08f;
        m.orientedBox({x, 0.55f, z}, {0.28f, 0.28f, 0.09f}, Quat::identity(), kTyre);
        m.orientedBox({x, 0.55f, -z}, {0.28f, 0.28f, 0.09f}, Quat::identity(), kTyre);
    }
}

} // namespace sea
