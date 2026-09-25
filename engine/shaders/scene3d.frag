#version 450
layout(location=0) in vec3 view_normal;
layout(location=1) in vec2 texture_uv;
layout(location=2) in vec3 view_position;
layout(location=3) in vec3 shadow_position;
layout(location=4) flat in uint instance_index;
layout(set=2,binding=0) uniform sampler2D surface_map;
layout(set=2,binding=1) uniform sampler2D optical_map;
layout(set=2,binding=2) uniform sampler2D environment_map;
layout(set=2,binding=3) uniform sampler2D normal_map;
layout(set=2,binding=4) uniform sampler2D properties_map;
layout(set=2,binding=5) uniform sampler2D cloud_map;
layout(set=2,binding=6) uniform sampler2D shadow_map;
layout(set=2,binding=7) uniform sampler2D sequence_map;
layout(set=2,binding=8) uniform sampler2D emissive_map;
layout(set=2,binding=9) uniform sampler2D metallic_roughness_map;
// Key-light depth map — a depth-only ortho pass ahead of the scene pass.
layout(set=2,binding=10) uniform sampler2D shadow_depth_map;
struct Material {
    vec4 tint;
    vec4 light_direction;
    vec4 parameters; // ambient, diffuse, opacity, dark-side mask strength
    vec4 optics; // IOR (zero disables optics), roughness, transmission, thickness
    vec4 absorption; // RGB coefficients, environment strength
    vec4 view_options; // orthographic, specular strength, scaled surface relief
    vec4 camera_orientation;
    vec4 illumination; // RGB star color, exposure-adjusted irradiance
    vec4 surface_response; // enabled, normal strength, scaled relief, cloud opacity
    vec4 surface_options; // cloud UV offset, rim power, two-sided diffuse
    vec4 shadow_light; // blocker-space direction, shape: 0 disabled, 1 ellipsoid, 2 annulus
    vec4 shadow_radii;
    vec4 shadow_options; // annulus inner, outer, opacity, has opacity map
    vec4 effect_options; // enabled, blend, flow phase, distortion
    vec4 effect_sphere; // view-space center and radius; zero disables
    vec4 volume_options; // local half-depth, ray steps, optical density, seed
    mat4 effect_from_view;
    vec4 additional_direction[2];
    vec4 additional_illumination[2];
    vec4 additional_shadow[2];
    vec4 texture_options; // cubic magnification enabled
    vec4 pbr_options; // enabled, packed map bound, night-emissive gate, alpha threshold
    vec4 pbr_values; // metallic, roughness, emissive strength, environment strength
    vec4 emissive_tint; // rgb, unused
    vec4 uv_options; // surface tiling x, y
    vec4 atmo_options; // tint rgb, strength
    vec4 atmo_shape; // rim power, nightside floor
    vec4 response_options; // terminator wrap, cloud albedo, map flags (1 normal, 2 properties, 4 cloud), limb darkening
    vec4 point_position[4]; // view-space position, range (0 = unbounded)
    vec4 point_energy[4]; // rgb, intensity
};
layout(set=2,binding=11,std430) readonly buffer Materials {
    Material materials[];
};
// Per-view diagnostic shading selector (DebugView3D): 0 lit, 1 unlit,
// 2 albedo, 3 normals, 4 roughness, 5 metallic, 6 emissive, 7 lighting.
layout(set=3,binding=0) uniform ViewParams {
    vec4 debug_mode;
    // view → shadow-map clip space, then texel size / strength / bias /
    // enabled in shadow_options
    mat4 shadow_from_view;
    vec4 shadow_options;
} view_params;
// Per-invocation material copy — populated from the instance-indexed buffer
// at the top of main so helper functions keep their shared access.
Material material;
layout(location=0) out vec4 color;
const float PI=3.14159265359;
vec4 cubic_weights(float t) {
    float t2=t*t,t3=t2*t;
    return vec4(-.5*t+t2-.5*t3,1.0-2.5*t2+1.5*t3,.5*t+2.0*t2-1.5*t3,-.5*t2+.5*t3);
}
vec4 surface_sample(sampler2D image,vec2 uv) {
    vec4 filtered=texture(image,uv);
    if(material.texture_options.x<.5) return filtered;
    vec2 size=vec2(textureSize(image,0));
    float footprint=max(length(dFdx(uv)*size),length(dFdy(uv)*size));
    if(footprint>=1.0) return filtered;
    vec2 p=uv*size-.5,base=floor(p),fraction=p-base;
    vec4 wx=cubic_weights(fraction.x),wy=cubic_weights(fraction.y),result=vec4(0);
    vec4 low=vec4(1),high=vec4(0);
    ivec2 limit=textureSize(image,0)-1;
    for(int y=0;y<4;++y)for(int x=0;x<4;++x){
        vec4 sample_color=texelFetch(image,clamp(ivec2(base)+ivec2(x-1,y-1),ivec2(0),limit),0);
        result+=sample_color*wx[x]*wy[y];
        if(x>=1&&x<=2&&y>=1&&y<=2){low=min(low,sample_color);high=max(high,sample_color);}
    }
    // Bounded to surrounding source pixels: no invented bright halos or ringing.
    return mix(clamp(result,low,high),filtered,smoothstep(.8,1.0,footprint));
}
vec3 world_direction(vec3 d) {
    return d+2.0*cross(material.camera_orientation.xyz,cross(material.camera_orientation.xyz,d)+material.camera_orientation.w*d);
}
vec3 radiance(vec3 d) {
    d=normalize(world_direction(d));
    vec2 uv=vec2(atan(d.x,d.z)/(2.0*PI)+0.5,acos(clamp(d.y,-1.0,1.0))/PI);
    // atan jumps by a turn at the longitude seam; that is not a large pixel
    // footprint. Keep the shortest wrapped derivatives for mip selection.
    vec2 dx=dFdx(uv),dy=dFdy(uv);dx.x-=round(dx.x);dy.x-=round(dy.x);
    return textureGrad(environment_map,uv,dx,dy).rgb;
}
// Deterministic cone filter: broad frost reflections without frame-to-frame noise.
vec3 environment(vec3 d,float roughness) {
    vec3 tangent=normalize(cross(abs(d.y)<0.95?vec3(0,1,0):vec3(1,0,0),d));
    vec3 bitangent=cross(d,tangent);
    float spread=roughness*roughness*0.8;
    return (radiance(d)*4.0+ radiance(d+spread*tangent)+radiance(d-spread*tangent)
        +radiance(d+spread*bitangent)+radiance(d-spread*bitangent))*0.125;
}
float fresnel(float cosine,float f0) {return f0+(1.0-f0)*pow(1.0-cosine,5.0);}
vec3 fresnel3(float cosine,vec3 f0) {return f0+(vec3(1.0)-f0)*pow(1.0-cosine,5.0);}
float direct_visibility(vec3 blocker_light) {
    if(material.shadow_light.w<0.5) return 1.0;
    vec3 P=shadow_position,L=blocker_light;
    float blocked=0.0;
    if(material.shadow_light.w<1.5) {
        vec3 p=P/material.shadow_radii.xyz,d=L/material.shadow_radii.xyz;
        float a=dot(d,d),b=dot(p,d),c=dot(p,p)-1.0;
        float discriminant=b*b-a*c;
        float edge=max(fwidth(discriminant),0.0000001);
        float exit_distance=(-b+sqrt(max(discriminant,0.0)))/a;
        if(exit_distance>0.0001) blocked=smoothstep(0.0,edge,discriminant);
    } else {
        if(abs(L.y)<0.000001) return 1.0;
        float distance=-P.y/L.y;
        vec3 hit=P+L*distance;
        float radius=length(hit.xz),edge=max(fwidth(radius),0.000001);
        blocked=step(0.0001,distance)*smoothstep(material.shadow_options.x-edge,material.shadow_options.x+edge,radius)
            *(1.0-smoothstep(material.shadow_options.y-edge,material.shadow_options.y+edge,radius));
        if(material.shadow_options.w>0.5) {
            vec2 uv=vec2(clamp((radius-material.shadow_options.x)/(material.shadow_options.y-material.shadow_options.x),0.0,1.0),
                fract(atan(hit.z,hit.x)/(2.0*PI)));
            // Integrate radial alpha over a pixel footprint. Narrow ring bands
            // otherwise form moire when projected onto a curved surface.
            float footprint=min(fwidth(uv.x),1.0),alpha=0.0;
            for(int tap=0;tap<8;++tap)
                alpha+=texture(shadow_map,vec2(clamp(uv.x+((float(tap)+0.5)/8.0-0.5)*footprint,0.0,1.0),uv.y)).a;
            blocked*=alpha/8.0;
        }
    }
    return 1.0-material.shadow_options.z*blocked;
}
vec3 linear_color(vec3 c) {return mix(c/12.92,pow((c+0.055)/1.055,vec3(2.4)),step(vec3(0.04045),c));}
vec3 display_color(vec3 c) {c=max(c,vec3(0));return mix(c*12.92,1.055*pow(c,vec3(1.0/2.4))-0.055,step(vec3(0.0031308),c));}
vec4 emission_volume(vec3 V) {
    // Integrate the interior of a closed proxy, exactly once per camera ray.
    // All density/flow lives in object space: it has parallax and cannot turn
    // into a camera-facing card as the stellar attachment rotates to the limb.
    vec3 origin=(material.effect_from_view*vec4(view_position,1)).xyz;
    vec3 direction=mat3(material.effect_from_view)*(-V);
    float scale=1.0/length(direction);direction=normalize(direction);
    float footprint=max(length(dFdx(origin)),length(dFdy(origin)));
    float mip=max(0.0,log2(max(footprint*float(textureSize(surface_map,0).x),1.0)));
    if(dot(normalize(view_normal),V)<=0.0) discard;
    vec3 lower=vec3(-.5,-.22,-material.volume_options.x),upper=vec3(.5,.78,material.volume_options.x);
    // Signed epsilon keeps parallel rays finite on proxy edges.
    vec3 safe_direction=mix(vec3(-1),vec3(1),greaterThanEqual(direction,vec3(0)))*max(abs(direction),vec3(.000001));
    vec3 t0=(lower-origin)/safe_direction,t1=(upper-origin)/safe_direction;
    vec3 first=min(t0,t1),last=max(t0,t1);
    float entry=max(0.0,max(first.x,max(first.y,first.z)));
    float exit_distance=min(last.x,min(last.y,last.z));
    // Stop at the near photosphere, retaining foreground plasma and extensions
    // beyond the stellar limb. This clips the whole ray, not just proxy faces.
    if(material.effect_sphere.w>0.0){
        vec3 p=view_position-material.effect_sphere.xyz;
        float b=dot(p,V),d=b*b-dot(p,p)+material.effect_sphere.w*material.effect_sphere.w;
        if(d>0.0&&b+sqrt(d)>0.0) exit_distance=min(exit_distance,max(0.0,(b-sqrt(d))/scale));
    }
    if(exit_distance<=entry) discard;
    int steps=int(material.volume_options.y);
    float step_size=(exit_distance-entry)/float(steps);
    vec4 integrated=vec4(0);
    float phase=material.effect_options.z,seed=material.volume_options.w;
    for(int step=0;step<64;++step){
        if(step>=steps||integrated.a>.985) break;
        vec3 p=origin+direction*(entry+(float(step)+.5)*step_size);
        vec2 uv=vec2(p.x+.5,.78-p.y);
        float vertical=sin(PI*clamp(uv.y,0.0,1.0));
        // Coherent depth-dependent shear and twisting preserve the supplied
        // image's broad shape while giving front/back filaments different paths.
        float z=p.z/material.volume_options.x;
        uv.x+=vertical*(.065*z*sin(uv.y*8.0+phase*.3+seed)+material.effect_options.w*sin(p.y*19.0+phase+z*3.0));
        uv.y+=vertical*.035*z*sin(p.x*11.0-phase*.4+seed);
        vec2 margin=min(uv,vec2(1)-uv);
        float edge=smoothstep(0.0,.085,min(margin.x,margin.y));
        vec4 a=textureLod(surface_map,uv,mip),b=textureLod(sequence_map,uv,mip);
        vec4 texel=mix(vec4(a.rgb*a.a,a.a),vec4(b.rgb*b.a,b.a),material.effect_options.y);
        vec3 emission=texel.a>.00001?texel.rgb/texel.a:vec3(0);
        // Rounded cross sections, a fine filament core and a faint envelope.
        float axis=.16*sin(p.y*10.0+seed+phase*.22)*vertical;
        float thickness=.24+.68*sqrt(clamp(texel.a,0.0,1.0));
        float cross_section=exp(-2.8*pow((z-axis)/thickness,2.0));
        cross_section*=1.0-smoothstep(.78,1.0,abs(z));
        float folds=sin(p.x*43.0+p.z*37.0+seed+phase*.6)*sin(p.y*31.0-p.z*29.0-phase*.45);
        float density=pow(max(texel.a,0.0),.85)*edge*cross_section*(.72+.28*folds);
        // Beer-Lambert integration is stable across quality levels and zoom.
        float alpha=1.0-exp(-density*material.volume_options.z*step_size);
        emission*=.85+.3*sqrt(max(density,0.0));
        integrated.rgb+=(1.0-integrated.a)*emission*alpha;
        integrated.a+=(1.0-integrated.a)*alpha;
    }
    integrated*=material.parameters.z*material.tint.a;integrated.rgb*=material.tint.rgb;
    if(integrated.a<.001) discard;
    return integrated;
}
void main() {
    material=materials[instance_index];
    if(material.volume_options.x>0.0){
        vec3 V=material.view_options.x>0.5?vec3(0,0,1):normalize(-view_position);
        color=emission_volume(V);return;
    }
    // Surface UV tiling feeds every texture-space sample; repeat samplers on
    // the bound maps make (1,1) identical to the untiled path.
    vec2 uv=texture_uv*material.uv_options.xy;
    // Evaluate derivatives before per-pixel alpha rejection; annulus horizon
    // rejection above is arithmetic so neighbouring fragments remain coherent.
    float visibility=direct_visibility(material.shadow_light.xyz);
    // Directional shadow map: receivers project into the key light's ortho
    // box. Coverage is an authored strategy-scale policy — fragments outside
    // the box stay lit instead of smearing. Fixed 8-tap kernel keeps PCF
    // cheap; the radius (in texels) and strength come from the quality tier.
    if(view_params.shadow_options.x>0.0) {
        vec4 clip=view_params.shadow_from_view*vec4(view_position,1.0);
        vec3 ndc=clip.xyz/max(clip.w,1e-9);
        vec2 suv=ndc.xy*0.5+0.5;
        if(clip.w>0.0&&suv.x>=0.0&&suv.x<=1.0&&suv.y>=0.0&&suv.y<=1.0&&ndc.z>=0.0&&ndc.z<=1.0) {
            float texel=view_params.shadow_options.x;
            float radius=view_params.shadow_options.y*texel;
            float bias=view_params.shadow_options.w;
            float lit;
            if(radius>0.0) {
                vec2 taps[8]=vec2[](vec2(-1.0,-1.0),vec2(1.0,-1.0),vec2(-1.0,1.0),vec2(1.0,1.0),
                                    vec2(-0.4,0.0),vec2(0.4,0.0),vec2(0.0,-0.4),vec2(0.0,0.4));
                lit=0.0;
                for(int i=0;i<8;++i)
                    lit+=step(ndc.z-bias,texture(shadow_depth_map,suv+taps[i]*radius).r);
                lit*=0.125;
            } else
                lit=step(ndc.z-bias,texture(shadow_depth_map,suv).r);
            visibility*=mix(1.0,lit,view_params.shadow_options.z);
        }
    }
    float extra_visibility[2];
    for(int i=0;i<2;++i)extra_visibility[i]=material.additional_illumination[i].a>0.0?direct_visibility(material.additional_shadow[i].xyz):1.0;
    vec3 N=normalize(view_normal);
    vec3 V=material.view_options.x>0.5?vec3(0,0,1):normalize(-view_position);
    vec4 properties=vec4(1,0,0,0);
    float cloud_shadow=1.0;
    vec4 cloud_layer=vec4(0);
    if(material.surface_response.x>0.5) {
        float map_flags=material.response_options.z;
        bool has_normal=mod(map_flags,2.0)>0.5;
        bool has_properties=mod(floor(map_flags/2.0),2.0)>0.5;
        bool has_cloud=mod(floor(map_flags/4.0),2.0)>0.5;
        if(has_properties)
            properties=texture(properties_map,uv);
        vec3 dx=dFdx(view_position),dy=dFdy(view_position);
        vec2 du=dFdx(texture_uv),dv=dFdy(texture_uv);
        vec3 T=dx*dv.y-dy*du.y;
        vec3 B=dy*du.x-dx*dv.x;
        float det=du.x*dv.y-du.y*dv.x;
        if(has_normal&&abs(det)>1e-12&&length(T)>1e-10&&length(B)>1e-10) {
            T=normalize(T*sign(det));B=normalize(B*sign(det));
            vec3 mapN=texture(normal_map,uv).xyz*2.0-1.0;
            N=normalize(N+material.surface_response.y*(T*mapN.x+B*mapN.y));
        }
        vec3 rx=cross(dy,N),ry=cross(N,dx);
        float d=dot(dx,rx);
        if(has_properties&&abs(d)>1e-15&&material.surface_response.z>0.0)
            N=normalize(abs(d)*N-material.surface_response.z*sign(d)*(dFdx(properties.a)*rx+dFdy(properties.a)*ry));
        // The sampler wraps longitude, preserving continuous pixel derivatives.
        if(has_cloud) {
            cloud_layer=texture(cloud_map,uv+material.surface_options.xy);
            cloud_shadow=1.0-material.surface_response.w*cloud_layer.a;
        }
    }
    float incidence=dot(N,material.light_direction.xyz);
    // Wrap-diffuse terminator: (N.L + w)/(1 + w) softens the day/night edge
    // without touching the fully lit or fully dark poles; 0 keeps Lambert.
    float terminator_wrap=material.response_options.x;
    float sunlight=material.surface_options.w>0.5?abs(incidence):clamp((incidence+terminator_wrap)/(1.0+terminator_wrap),0.0,1.0);
    vec2 sample_uv=uv;
    if(material.effect_options.x>0.5){
        // Smooth bounded flow changes filament shape without wrapping the image.
        float envelope=sin(PI*uv.x)*sin(PI*uv.y);
        sample_uv+=material.effect_options.w*envelope*vec2(sin(texture_uv.y*19.0+material.effect_options.z),cos(texture_uv.x*13.0-material.effect_options.z*.7));
    }
    vec4 texel=surface_sample(surface_map,sample_uv);
    if(material.effect_options.x>0.5){
        vec4 next=surface_sample(sequence_map,sample_uv);
        // Interpolate radiance in premultiplied space to avoid black fringes.
        vec4 premul=mix(vec4(texel.rgb*texel.a,texel.a),vec4(next.rgb*next.a,next.a),material.effect_options.y);
        texel=vec4(premul.a>0.00001?premul.rgb/premul.a:vec3(0),premul.a);
        // Some supplied plasma plumes touch their square image boundary.
        // Taper only the outer margin so the ribbon never exposes a cut edge.
        vec2 margin=min(texture_uv,vec2(1)-texture_uv);
        texel.a*=smoothstep(0.0,0.07,min(margin.x,margin.y));
        if(material.effect_sphere.w>0.0){
            vec3 p=view_position-material.effect_sphere.xyz;
            float b=dot(p,V),d=b*b-dot(p,p)+material.effect_sphere.w*material.effect_sphere.w;
            float entry=-b-sqrt(max(d,0.0)),exit_distance=-b+sqrt(max(d,0.0));
            float edge=max(fwidth(d),0.00001);
            float blocked=smoothstep(0.0,edge,d)*step(0.0001,exit_distance);
            if(entry>0.0||dot(p,p)<material.effect_sphere.w*material.effect_sphere.w)texel.a*=1.0-blocked;
        }
    }
    vec3 raw_albedo=texel.rgb; // debug Albedo view: sampled surface, pre-tint
    texel*=material.tint;
    if(material.view_options.w>0.5) texel.rgb=linear_color(texel.rgb);
    float combined_sunlight=sunlight;
    for(int i=0;i<2;++i)if(material.additional_illumination[i].a>0.0)combined_sunlight=max(combined_sunlight,max(dot(N,material.additional_direction[i].xyz),0.0));
    float alpha=texel.a*material.parameters.z*clamp(1.0-combined_sunlight*material.parameters.w,0.0,1.0);
    if(material.surface_options.z>0.0) alpha*=pow(1.0-abs(dot(N,V)),material.surface_options.z);
    // Alpha cutout rejects before lighting cost; surviving fragments keep
    // their computed alpha (opaque and transparent cutouts both work).
    if(material.pbr_options.w>0.0&&alpha<material.pbr_options.w)discard;
    // Metallic-workflow factors: packed map (G roughness, B metallic) scales
    // the scalar values when bound.
    bool pbr_active=material.pbr_options.x>0.5;
    float metallic=0.0,pbr_roughness=material.pbr_values.y;
    if(pbr_active){
        if(material.pbr_options.y>0.5){
            vec3 mr=texture(metallic_roughness_map,uv).rgb;
            metallic=clamp(mr.b,0.0,1.0);pbr_roughness=clamp(mr.g*pbr_roughness,0.04,1.0);
        }else{
            metallic=clamp(material.pbr_values.x,0.0,1.0);
            pbr_roughness=clamp(pbr_roughness,0.04,1.0);
        }
    }
    float diffuse_scale=pbr_active?1.0-metallic:1.0;
    // Debug Roughness/Metallic views report the factors actually in play.
    float dbg_roughness=pbr_active?pbr_roughness
        :(material.surface_response.x>0.5?clamp(properties.r,0.0,1.0)
        :(material.optics.x>=1.0?clamp(material.optics.y,0.0,1.0):0.55));
    vec3 light_color=material.illumination.rgb*material.illumination.a;
    vec3 result=texel.rgb*diffuse_scale*(material.parameters.x+material.parameters.y*sunlight*cloud_shadow*visibility*light_color);
    if(material.surface_response.x>0.5||pbr_active) {
        vec3 sum=V+material.light_direction.xyz;
        vec3 H=length(sum)>0.0001?normalize(sum):N;
        float roughness=pbr_active?pbr_roughness:clamp(properties.r,0.10,1.0);
        float nv=max(dot(N,V),0.001),nh=max(dot(N,H),0.0);
        float a2=pow(roughness,4.0),den=nh*nh*(a2-1.0)+1.0;
        float D=a2/max(PI*den*den,0.00001),k=pow(roughness+1.0,2.0)/8.0;
        float G=(nv/(nv*(1.0-k)+k))*(sunlight/(sunlight*(1.0-k)+k));
        vec3 F=pbr_active?fresnel3(max(dot(V,H),0.0),mix(vec3(.04),texel.rgb,metallic))
            :vec3(fresnel(max(dot(V,H),0.0),mix(.035,.020,properties.g)));
        float strength=pbr_active?1.0:.12+.88*max(properties.g,properties.b);
        result+=light_color*cloud_shadow*visibility*strength*D*G*F/max(4.0*nv,.001);
    }
    if(pbr_active&&material.pbr_values.w>0.0){
        // Diffuse irradiance + roughness-aware specular environment response.
        float nv=max(dot(N,V),0.001);
        vec3 f0=mix(vec3(.04),texel.rgb,metallic);
        vec3 fresnel_env=f0+(max(vec3(1.0-pbr_roughness),f0)-f0)*pow(1.0-nv,5.0);
        result+=material.pbr_values.w*(texel.rgb*(1.0-metallic)*environment(N,1.0)
            +environment(reflect(-V,N),pbr_roughness)*fresnel_env);
    }
    if(material.optics.x>=1.0) {
        vec3 V=material.view_options.x>0.5?vec3(0,0,1):normalize(-view_position);
        if(!gl_FrontFacing) N=-N;
        vec4 optical_texel=texture(optical_map,uv);
        vec3 mask=optical_texel.rgb;
        vec3 dx=dFdx(view_position),dy=dFdy(view_position);
        vec3 rx=cross(dy,N),ry=cross(N,dx);
        float determinant=dot(dx,rx);
        if(abs(determinant)>1e-15&&material.view_options.z>0.0) {
            vec3 gradient=sign(determinant)*(dFdx(optical_texel.a)*rx+dFdy(optical_texel.a)*ry);
            N=normalize(abs(determinant)*N-material.view_options.z*gradient);
        }
        sunlight=max(dot(N,material.light_direction.xyz),0.0);
        result=texel.rgb*clamp(material.parameters.x+material.parameters.y*sunlight*visibility,0.0,1.0);
        float roughness=clamp(material.optics.y*mask.r,0.04,1.0);
        float transmission=material.optics.z*mask.g;
        float nv=max(dot(N,V),0.001);
        float f0=pow((material.optics.x-1.0)/(material.optics.x+1.0),2.0);
        float F=fresnel(nv,f0);
        vec3 R=reflect(-V,N);
        vec3 T=refract(-V,N,gl_FrontFacing?1.0/material.optics.x:material.optics.x);
        bool total_internal=dot(T,T)<0.0001;
        if(total_internal) {T=R;F=1.0;}
        float path=material.optics.w*mask.b/max(abs(dot(N,T)),0.15);
        vec3 transmitted=environment(T,roughness)*exp(-material.absorption.rgb*path);
        vec3 reflected=environment(R,roughness);
        result=result*(1.0-F)*(1.0-transmission)
            +material.absorption.w*(reflected*F+transmitted*(1.0-F)*transmission);
        // GGX microfacet sun reflection, Smith masking and Schlick Fresnel.
        vec3 sum=V+material.light_direction.xyz;
        vec3 H=length(sum)>0.0001?normalize(sum):N;
        float nl=max(dot(N,material.light_direction.xyz),0.0),nh=max(dot(N,H),0.0);
        float a2=pow(roughness,4.0);
        float denominator=nh*nh*(a2-1.0)+1.0;
        float D=a2/max(PI*denominator*denominator,0.000001);
        float k=pow(roughness+1.0,2.0)/8.0;
        float G=(nv/(nv*(1.0-k)+k))*(nl/(nl*(1.0-k)+k));
        float specular=D*G*fresnel(max(dot(V,H),0.0),f0)/max(4.0*nv,0.001);
        result+=vec3(specular*material.view_options.y*visibility);
    }
    for(int i=0;i<2;++i) {
        if(material.additional_illumination[i].a<=0.0) continue;
        vec3 L=material.additional_direction[i].xyz;
        float nl=material.surface_options.w>0.5?abs(dot(N,L)):clamp((dot(N,L)+terminator_wrap)/(1.0+terminator_wrap),0.0,1.0);
        vec3 energy=material.additional_illumination[i].rgb*material.additional_illumination[i].a*extra_visibility[i]*cloud_shadow;
        result+=texel.rgb*diffuse_scale*material.parameters.y*nl*energy;
        if(material.surface_response.x>0.5||material.optics.x>=1.0||pbr_active) {
            vec3 sum=V+L,H=length(sum)>0.0001?normalize(sum):N;
            float roughness=clamp(material.optics.x>=1.0?material.optics.y:(pbr_active?pbr_roughness:properties.r),.1,1.0);
            float nv=max(dot(N,V),.001),nh=max(dot(N,H),0.0),a2=pow(roughness,4.0);
            float den=nh*nh*(a2-1.0)+1.0,k=pow(roughness+1.0,2.0)/8.0;
            float D=a2/max(PI*den*den,.00001),G=nv/(nv*(1.0-k)+k)*nl/(nl*(1.0-k)+k);
            float strength=material.optics.x>=1.0?material.view_options.y:(pbr_active?1.0:.12+.88*max(properties.g,properties.b));
            if(pbr_active&&material.optics.x<1.0){
                vec3 f0=mix(vec3(.04),texel.rgb,metallic);
                result+=energy*strength*D*G*fresnel3(max(dot(V,H),0.0),f0)/max(4.0*nv,.001);
            }else{
                float f0=material.optics.x>=1.0?pow((material.optics.x-1.0)/(material.optics.x+1.0),2.0):mix(.035,.020,properties.g);
                result+=energy*strength*D*G*fresnel(max(dot(V,H),0.0),f0)/max(4.0*nv,.001);
            }
        }
    }
    // Scene point lights: windowed inverse-square attenuation keeps distant
    // receivers at zero cost and bounded brightness at contact range.
    for(int i=0;i<4;++i){
        if(material.point_energy[i].w<=0.0) continue;
        vec3 to_light=material.point_position[i].xyz-view_position;
        float d2=dot(to_light,to_light);
        vec3 L=to_light*inversesqrt(max(d2,0.00000001));
        float range=material.point_position[i].w;
        float window=1.0;
        if(range>0.0){float x=clamp(sqrt(d2)/range,0.0,1.0);window=pow(1.0-x*x*x*x,2.0);}
        if(window<=0.0) continue;
        vec3 energy=material.point_energy[i].rgb*(material.point_energy[i].w*window/max(d2,0.0001))*cloud_shadow;
        float nl=material.surface_options.w>0.5?abs(dot(N,L)):clamp((dot(N,L)+terminator_wrap)/(1.0+terminator_wrap),0.0,1.0);
        result+=texel.rgb*diffuse_scale*material.parameters.y*nl*energy;
        if(material.surface_response.x>0.5||material.optics.x>=1.0||pbr_active){
            vec3 sum=V+L,H=length(sum)>0.0001?normalize(sum):N;
            float roughness=clamp(material.optics.x>=1.0?material.optics.y:(pbr_active?pbr_roughness:properties.r),.1,1.0);
            float nv=max(dot(N,V),.001),nh=max(dot(N,H),0.0),a2=pow(roughness,4.0);
            float den=nh*nh*(a2-1.0)+1.0,k=pow(roughness+1.0,2.0)/8.0;
            float D=a2/max(PI*den*den,.00001),G=nv/(nv*(1.0-k)+k)*nl/(nl*(1.0-k)+k);
            float strength=material.optics.x>=1.0?material.view_options.y:(pbr_active?1.0:.12+.88*max(properties.g,properties.b));
            if(pbr_active&&material.optics.x<1.0){
                vec3 f0=mix(vec3(.04),texel.rgb,metallic);
                result+=energy*strength*D*G*fresnel3(max(dot(V,H),0.0),f0)/max(4.0*nv,.001);
            }else{
                float f0=material.optics.x>=1.0?pow((material.optics.x-1.0)/(material.optics.x+1.0),2.0):mix(.035,.020,properties.g);
                result+=energy*strength*D*G*fresnel(max(dot(V,H),0.0),f0)/max(4.0*nv,.001);
            }
        }
    }
    // Emissive overlay — tinted map radiance, optionally gated to the body's
    // unlit hemisphere for colony/city lights that fade across the terminator.
    // emissive_part also feeds the Emissive debug view (and is excluded from
    // LightingOnly's albedo division so glow stays readable).
    vec3 emissive_part=vec3(0);
    if(pbr_active&&material.pbr_values.z>0.0){
        float gate=1.0;
        if(material.pbr_options.z>0.0)
            gate=mix(1.0,1.0-smoothstep(-0.1,0.3,incidence),material.pbr_options.z);
        emissive_part+=texture(emissive_map,uv).rgb*material.emissive_tint.rgb*material.pbr_values.z*gate;
        result+=emissive_part;
    }
    // Visible cloud deck: the cloud map's RGB composites over the lit
    // surface — including surface emissive, which a cloud cover occludes —
    // lit by the same wrapped sunlight. Coverage scales with the same
    // opacity that drives the deck's surface shadow; albedo scales deck
    // brightness so a zero albedo leaves the layer shadow-only.
    if(material.response_options.y>0.0&&material.surface_response.w>0.0) {
        float cover=clamp(cloud_layer.a*material.surface_response.w,0.0,1.0);
        vec3 deck=cloud_layer.rgb*material.response_options.y*(material.parameters.x+material.parameters.y*sunlight*visibility*light_color);
        result=mix(result,deck,cover);
    }
    // Linear limb darkening: emitted/reflected radiance falls toward the
    // disc edge (Sun u ~= 0.6), so HDR photosphere discs keep a physical
    // profile instead of clipping flat. The geometric normal decides the
    // profile — normal-mapped detail is not limb darkening. The additive
    // atmosphere rim below is exempt: it is a scattering shell, not the
    // photosphere.
    if(material.response_options.w>0.0)
        result*=1.0-material.response_options.w*(1.0-clamp(dot(normalize(view_normal),V),0.0,1.0));
    // Single-scatter limb: wavelength-tinted rim, day-side weighted with a
    // nightside floor, tied to the star's actual color.
    if(material.atmo_options.w>0.0){
        vec3 Ng=normalize(view_normal);
        float limb=pow(clamp(1.0-abs(dot(Ng,V)),0.0,1.0),material.atmo_shape.x);
        float day=max(material.atmo_shape.y,smoothstep(-0.25,0.3,dot(Ng,material.light_direction.xyz)));
        vec3 rim=material.atmo_options.rgb*material.atmo_options.w*limb*day*light_color;
        emissive_part+=rim;result+=rim;
    }
    // Preserve legacy diffuse materials and premultiplied composition.
    if(alpha<0.001) discard;
    int debug=int(view_params.debug_mode.x+0.5);
    if(debug>0){
        vec3 shown;
        if(debug==1) shown=texel.rgb;                              // Unlit
        else if(debug==2) shown=raw_albedo;                        // Albedo
        else if(debug==3) shown=N*0.5+0.5;                         // Normals
        else if(debug==4) shown=vec3(dbg_roughness);               // Roughness
        else if(debug==5) shown=vec3(metallic);                    // Metallic
        else if(debug==6) shown=emissive_part;                     // Emissive
        else shown=(result-emissive_part)/max(texel.rgb,vec3(.001));// Lighting
        shown=max(shown,vec3(0));
        if(material.view_options.w>0.5) shown=display_color(shown);
        color=vec4(shown*alpha,alpha);return;
    }
    if(material.view_options.w>0.5) result=display_color(result);
    color=vec4(result*alpha,alpha);
}
