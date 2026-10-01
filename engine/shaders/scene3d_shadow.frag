#version 450
layout(location=0) flat in float keep;
layout(location=1) flat in vec3 cutout;
layout(location=2) in vec2 tex_uv;
layout(set=2,binding=0) uniform sampler2D caster_map;
// Depth-only pass: writes no colour, but the signed keep mask screen-doors
// the caster exactly like the scene pass (positive keeps ign<w, negative
// keeps the complementary ign>=1+w fraction), so a LOD/range/group
// transition thins the shadow silhouette instead of popping it. Casters
// whose material sets an alpha threshold also drop the texels the lit
// pass discards, so a holed texture casts a perforated shadow instead of
// its full quad footprint.
void main() {
    if(cutout.x>0.0&&texture(caster_map,tex_uv*cutout.yz).a<cutout.x)discard;
    if(keep<1.0){
        const vec2 fc=floor(gl_FragCoord.xy);
        const float ign=fract(52.9829189*fract(fc.x*0.06711056+fc.y*0.00583715));
        if(keep>=0.0?ign>=keep:ign<1.0+keep)discard;
    }
}
