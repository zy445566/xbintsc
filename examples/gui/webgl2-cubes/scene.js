// WebGL 2 立方体阵列 — the shared scene.
//
// One VAO, one index buffer and **one instanced draw call** for 64 lit,
// textured cubes. The vertex shader is GLSL ES 3.00 (`#version 300 es`), reads a
// per-instance offset/phase attribute (`vertexAttribDivisor`) and rotates each
// cube by `u_time + phase`. A browser runs this file unchanged; xbintsc
// AOT-compiles it through the gui `.html` loader.
//
// The host reads `window.__webgl2Cubes` for its report.

(function () {
  "use strict";

  var canvas = document.getElementById("scene");
  var gl = canvas.getContext("webgl2");
  var state = {
    ok: false,
    frames: 0,
    error: -1,
    size: "",
    instances: 0,
    probe: function (x, y) {
      var pixel = new Uint8Array(4);
      gl.readPixels(x, y, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, pixel);
      return pixel[0] + "," + pixel[1] + "," + pixel[2] + "," + pixel[3];
    },
    probeMax: function (x, y, width, height) {
      var pixels = new Uint8Array(width * height * 4);
      gl.readPixels(x, y, width, height, gl.RGBA, gl.UNSIGNED_BYTE, pixels);
      var best = 0;
      var index = 0;
      for (var i = 0; i < width * height; i++) {
        var sum = pixels[i * 4] + pixels[i * 4 + 1] + pixels[i * 4 + 2];
        if (sum > best) {
          best = sum;
          index = i;
        }
      }
      return pixels[index * 4] + "," + pixels[index * 4 + 1] + "," + pixels[index * 4 + 2] + ",255";
    },
  };
  window.__webgl2Cubes = state;
  if (gl == null) {
    state.error = "no-webgl2";
    return;
  }

  /* -- matrices (column-major) ------------------------------------------ */

  function identity() {
    var m = new Float32Array(16);
    m[0] = 1;
    m[5] = 1;
    m[10] = 1;
    m[15] = 1;
    return m;
  }

  function multiply(a, b) {
    var out = new Float32Array(16);
    for (var column = 0; column < 4; column++) {
      for (var row = 0; row < 4; row++) {
        var sum = 0;
        for (var k = 0; k < 4; k++) sum += a[k * 4 + row] * b[column * 4 + k];
        out[column * 4 + row] = sum;
      }
    }
    return out;
  }

  function perspective(fovy, aspect, near, far) {
    var f = 1 / Math.tan(fovy / 2);
    var m = identity();
    m[0] = f / aspect;
    m[5] = f;
    m[10] = (far + near) / (near - far);
    m[11] = -1;
    m[14] = (2 * far * near) / (near - far);
    m[15] = 0;
    return m;
  }

  function translation(x, y, z) {
    var m = identity();
    m[12] = x;
    m[13] = y;
    m[14] = z;
    return m;
  }

  /* -- geometry: 24 vertices, 4 per face -------------------------------- */

  var positions = new Float32Array([
    -0.5, -0.5, 0.5, 0.5, -0.5, 0.5, 0.5, 0.5, 0.5, -0.5, 0.5, 0.5,
    0.5, -0.5, -0.5, -0.5, -0.5, -0.5, -0.5, 0.5, -0.5, 0.5, 0.5, -0.5,
    0.5, -0.5, 0.5, 0.5, -0.5, -0.5, 0.5, 0.5, -0.5, 0.5, 0.5, 0.5,
    -0.5, -0.5, -0.5, -0.5, -0.5, 0.5, -0.5, 0.5, 0.5, -0.5, 0.5, -0.5,
    -0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, -0.5, -0.5, 0.5, -0.5,
    -0.5, -0.5, -0.5, 0.5, -0.5, -0.5, 0.5, -0.5, 0.5, -0.5, -0.5, 0.5,
  ]);

  var normals = new Float32Array([
    0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1,
    0, 0, -1, 0, 0, -1, 0, 0, -1, 0, 0, -1,
    1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0,
    -1, 0, 0, -1, 0, 0, -1, 0, 0, -1, 0, 0,
    0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0,
    0, -1, 0, 0, -1, 0, 0, -1, 0, 0, -1, 0,
  ]);

  var uvs = new Float32Array([
    0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1,
    0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1,
    0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1,
  ]);

  var indices = new Uint16Array(36);
  for (var face = 0; face < 6; face++) {
    var base = face * 4;
    indices[face * 6 + 0] = base;
    indices[face * 6 + 1] = base + 1;
    indices[face * 6 + 2] = base + 2;
    indices[face * 6 + 3] = base;
    indices[face * 6 + 4] = base + 2;
    indices[face * 6 + 5] = base + 3;
  }

  /* -- one vec4 of instance data per cube -------------------------------- */

  var COUNT = 64;
  var instance_data = new Float32Array(COUNT * 4);
  for (var i = 0; i < COUNT; i++) {
    var ix = i % 4;
    var iy = Math.floor(i / 4) % 4;
    var iz = Math.floor(i / 16);
    instance_data[i * 4 + 0] = (ix - 1.5) * 1.15;
    instance_data[i * 4 + 1] = (iy - 1.5) * 1.15;
    instance_data[i * 4 + 2] = (iz - 1.5) * 1.15;
    instance_data[i * 4 + 3] = i * 0.41;
  }

  /* -- shaders (GLSL ES 3.00) -------------------------------------------- */

  var vertex_source =
    "#version 300 es\n" +
    "in vec3 a_position;\n" +
    "in vec3 a_normal;\n" +
    "in vec2 a_uv;\n" +
    "in vec4 a_instance;\n" +
    "uniform mat4 u_viewProj;\n" +
    "uniform float u_time;\n" +
    "out vec3 v_normal;\n" +
    "out vec2 v_uv;\n" +
    "void main() {\n" +
    "  float angle = u_time + a_instance.w;\n" +
    "  float c = cos(angle);\n" +
    "  float s = sin(angle);\n" +
    "  mat3 rotation = mat3(c, 0.0, -s, 0.0, 1.0, 0.0, s, 0.0, c);\n" +
    "  vec3 world = rotation * a_position + a_instance.xyz;\n" +
    "  v_normal = rotation * a_normal;\n" +
    "  v_uv = a_uv;\n" +
    "  gl_Position = u_viewProj * vec4(world, 1.0);\n" +
    "}\n";

  var fragment_source =
    "#version 300 es\n" +
    "precision highp float;\n" +
    "in vec3 v_normal;\n" +
    "in vec2 v_uv;\n" +
    "uniform sampler2D u_tex;\n" +
    "uniform vec3 u_light;\n" +
    "out vec4 fragColor;\n" +
    "void main() {\n" +
    "  vec3 n = normalize(v_normal);\n" +
    "  float diffuse = max(dot(n, normalize(u_light)), 0.0);\n" +
    "  vec3 base = texture(u_tex, v_uv).rgb;\n" +
    "  fragColor = vec4(base * (0.3 + 0.7 * diffuse), 1.0);\n" +
    "}\n";

  function compile(type, source) {
    var shader = gl.createShader(type);
    gl.shaderSource(shader, source);
    gl.compileShader(shader);
    if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
      state.error = gl.getShaderInfoLog(shader);
      return null;
    }
    return shader;
  }

  var vertex_shader = compile(gl.VERTEX_SHADER, vertex_source);
  var fragment_shader = compile(gl.FRAGMENT_SHADER, fragment_source);
  if (vertex_shader == null || fragment_shader == null) return;

  var program = gl.createProgram();
  gl.attachShader(program, vertex_shader);
  gl.attachShader(program, fragment_shader);
  gl.linkProgram(program);
  if (!gl.getProgramParameter(program, gl.LINK_STATUS)) {
    state.error = gl.getProgramInfoLog(program);
    return;
  }
  gl.useProgram(program);

  /* -- VAO ---------------------------------------------------------------- */

  var vao = gl.createVertexArray();
  gl.bindVertexArray(vao);

  function attributeBuffer(data, location, size) {
    var buffer = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
    gl.bufferData(gl.ARRAY_BUFFER, data, gl.STATIC_DRAW);
    gl.enableVertexAttribArray(location);
    gl.vertexAttribPointer(location, size, gl.FLOAT, false, 0, 0);
    return buffer;
  }

  attributeBuffer(positions, gl.getAttribLocation(program, "a_position"), 3);
  attributeBuffer(normals, gl.getAttribLocation(program, "a_normal"), 3);
  attributeBuffer(uvs, gl.getAttribLocation(program, "a_uv"), 2);
  var instance_location = gl.getAttribLocation(program, "a_instance");
  attributeBuffer(instance_data, instance_location, 4);
  gl.vertexAttribDivisor(instance_location, 1);

  var index_buffer = gl.createBuffer();
  gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, index_buffer);
  gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, indices, gl.STATIC_DRAW);
  gl.bindVertexArray(null);

  /* -- checker texture --------------------------------------------------- */

  var size = 64;
  var texels = new Uint8Array(size * size * 4);
  for (var y = 0; y < size; y++) {
    for (var x = 0; x < size; x++) {
      var on = (Math.floor(x / 8) + Math.floor(y / 8)) % 2 === 0;
      var shade = on ? 235 : 48;
      var at = (y * size + x) * 4;
      texels[at] = shade;
      texels[at + 1] = shade;
      texels[at + 2] = shade;
      texels[at + 3] = 255;
    }
  }
  var texture = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D, texture);
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, size, size, 0, gl.RGBA, gl.UNSIGNED_BYTE, texels);
  gl.generateMipmap(gl.TEXTURE_2D);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR_MIPMAP_LINEAR);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
  gl.uniform1i(gl.getUniformLocation(program, "u_tex"), 0);
  gl.uniform3f(gl.getUniformLocation(program, "u_light"), 0.5, 0.8, 0.4);

  var view_projection = multiply(perspective(0.9, canvas.width / canvas.height, 0.1, 40.0),
                                 translation(0.0, 0.0, -7.5));
  var view_projection_location = gl.getUniformLocation(program, "u_viewProj");
  var time_location = gl.getUniformLocation(program, "u_time");

  gl.enable(gl.DEPTH_TEST);
  gl.enable(gl.CULL_FACE);
  gl.cullFace(gl.BACK);
  gl.clearColor(0.04, 0.06, 0.1, 1.0);
  gl.viewport(0, 0, canvas.width, canvas.height);
  state.size = canvas.width + "x" + canvas.height;
  state.instances = COUNT;
  state.ok = true;

  function draw(seconds) {
    gl.uniformMatrix4fv(view_projection_location, false, view_projection);
    gl.uniform1f(time_location, seconds);
    gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
    gl.bindVertexArray(vao);
    gl.drawElementsInstanced(gl.TRIANGLES, 36, gl.UNSIGNED_SHORT, 0, COUNT);
    gl.bindVertexArray(null);
    state.frames = state.frames + 1;
    state.error = gl.getError();
  }

  var start = 0;
  function frame(now) {
    if (start === 0) start = now;
    draw((now - start) / 1000);
    window.requestAnimationFrame(frame);
  }
  window.requestAnimationFrame(frame);
})();
