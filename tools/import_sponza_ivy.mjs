#!/usr/bin/env node

import { createHash } from "node:crypto";
import {
  existsSync,
  mkdirSync,
  readFileSync,
  readdirSync,
  statSync,
  unlinkSync,
  writeFileSync,
} from "node:fs";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const scriptDir = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(scriptDir, "..");
const outputDir = path.join(
  repoRoot,
  "assets",
  "scenes",
  "intel_sponza",
  "components",
  "ivy",
);
const reportPath = path.join(outputDir, "import-report.json");

const GITHUB_FILE_LIMIT = 100_000_000;
const TRIM_WORLD_Y = 16.0414255217268;
const TRIM_EPSILON_METERS = 1e-5;
const KEEP_MAX_WORLD_Y = TRIM_WORLD_Y - TRIM_EPSILON_METERS;
const TEXTURE_SIZE = 1024;

const sourceFiles = Object.freeze({
  gltf: "NewSponza_IvyGrowth_glTF.gltf",
  bin: "NewSponza_IvyGrowth_glTF.bin",
  normal: path.join("textures", "IvyLeaf_Normal.png"),
  baseColor: path.join("textures", "IvyLeaf_BaseColor.png"),
  roughness: path.join("textures", "IvyLeaf_Roughness0.png"),
});

const outputNames = Object.freeze({
  part1: "intel_pbr_sponza_ivy_part1.glb",
  part2: "intel_pbr_sponza_ivy_part2.glb",
});
const obsoleteOutputNames = Object.freeze([
  "intel_pbr_sponza_ivy_leaves_part1.glb",
  "intel_pbr_sponza_ivy_leaves_part2.glb",
  "intel_pbr_sponza_ivy_lianas.glb",
]);

const FLOAT = 5126;
const UNSIGNED_INT = 5125;
const UNSIGNED_SHORT = 5123;
const ARRAY_BUFFER = 34962;
const ELEMENT_ARRAY_BUFFER = 34963;
const MAX_UNSIGNED_SHORT_VERTICES = 65_535;

function assert(condition, message) {
  if (!condition) {
    throw new Error(message);
  }
}

function sha256(buffer) {
  return createHash("sha256").update(buffer).digest("hex");
}

function align4(value) {
  return (value + 3) & ~3;
}

function relative(filePath) {
  return path.relative(repoRoot, filePath).replaceAll("\\", "/");
}

function expectedFile(sourceDir, name) {
  const filePath = path.join(sourceDir, name);
  assert(existsSync(filePath), "Missing ivy source file " + filePath);
  return filePath;
}

function componentCount(type) {
  switch (type) {
    case "SCALAR":
      return 1;
    case "VEC2":
      return 2;
    case "VEC3":
      return 3;
    case "VEC4":
      return 4;
    default:
      throw new Error("Unsupported accessor type " + type);
  }
}

function accessorLayout(source, accessorIndex) {
  const accessor = source.json.accessors[accessorIndex];
  assert(accessor, "Missing accessor " + accessorIndex);
  assert(!accessor.sparse, "Sparse ivy accessors are not supported");
  const bufferView = source.json.bufferViews[accessor.bufferView];
  assert(bufferView, "Missing bufferView for accessor " + accessorIndex);
  assert((bufferView.buffer ?? 0) === 0, "Ivy accessor is not in buffer zero");
  const components = componentCount(accessor.type);
  const componentBytes = accessor.componentType === UNSIGNED_INT ? 4 : 4;
  return {
    accessor,
    components,
    byteOffset: (bufferView.byteOffset ?? 0) + (accessor.byteOffset ?? 0),
    byteStride: bufferView.byteStride ?? components * componentBytes,
  };
}

function readFloatVector(source, layout, index, output) {
  assert(layout.accessor.componentType === FLOAT, "Accessor is not FLOAT");
  const offset = layout.byteOffset + index * layout.byteStride;
  for (let component = 0; component < layout.components; ++component) {
    output[component] = source.view.getFloat32(offset + component * 4, true);
  }
  return output;
}

function readIndex(source, layout, index) {
  assert(layout.accessor.componentType === UNSIGNED_INT, "Index accessor is not UINT32");
  return source.view.getUint32(layout.byteOffset + index * layout.byteStride, true);
}

function normalizedQuaternion(node) {
  const quaternion = node.rotation ?? [0, 0, 0, 1];
  const length = Math.hypot(...quaternion);
  assert(length > 0, "Ivy node has a zero quaternion");
  return quaternion.map((value) => value / length);
}

function rotateVector(quaternion, vector) {
  const [x, y, z, w] = quaternion;
  const [vx, vy, vz] = vector;
  return [
    (1 - 2 * y * y - 2 * z * z) * vx +
      (2 * x * y - 2 * z * w) * vy +
      (2 * x * z + 2 * y * w) * vz,
    (2 * x * y + 2 * z * w) * vx +
      (1 - 2 * x * x - 2 * z * z) * vy +
      (2 * y * z - 2 * x * w) * vz,
    (2 * x * z - 2 * y * w) * vx +
      (2 * y * z + 2 * x * w) * vy +
      (1 - 2 * x * x - 2 * y * y) * vz,
  ];
}

function nodeTransform(node) {
  const scale = node.scale ?? [1, 1, 1];
  assert(
    Math.abs(scale[0] - scale[1]) < 1e-8 &&
      Math.abs(scale[1] - scale[2]) < 1e-8 &&
      scale[0] > 0,
    "Ivy importer requires a positive uniform node scale",
  );
  return {
    translation: node.translation ?? [0, 0, 0],
    rotation: normalizedQuaternion(node),
    scale: scale[0],
  };
}

function transformPosition(transform, local) {
  const rotated = rotateVector(
    transform.rotation,
    local.map((value) => value * transform.scale),
  );
  return rotated.map((value, axis) => value + transform.translation[axis]);
}

function transformDirection(transform, local) {
  const rotated = rotateVector(transform.rotation, local);
  const length = Math.hypot(...rotated);
  assert(length > 1e-12, "Ivy direction vector collapsed during transform");
  return rotated.map((value) => value / length);
}

function makeUnionFind(vertexCount) {
  const parents = new Int32Array(vertexCount);
  parents.fill(-1);
  const find = (value) => {
    let root = value;
    while (parents[root] >= 0) {
      root = parents[root];
    }
    while (value !== root) {
      const next = parents[value];
      parents[value] = root;
      value = next;
    }
    return root;
  };
  const unite = (left, right) => {
    left = find(left);
    right = find(right);
    if (left === right) {
      return;
    }
    if (parents[left] > parents[right]) {
      [left, right] = [right, left];
    }
    parents[left] += parents[right];
    parents[right] = left;
  };
  return { parents, find, unite };
}

function addBounds(bounds, position) {
  for (let axis = 0; axis < 3; ++axis) {
    bounds.min[axis] = Math.min(bounds.min[axis], position[axis]);
    bounds.max[axis] = Math.max(bounds.max[axis], position[axis]);
  }
}

function emptyBounds() {
  return {
    min: [Infinity, Infinity, Infinity],
    max: [-Infinity, -Infinity, -Infinity],
  };
}

function analyzePrimitive(source, nodeIndex, semanticName) {
  const node = source.json.nodes[nodeIndex];
  assert(node.mesh !== undefined, semanticName + " node has no mesh");
  const mesh = source.json.meshes[node.mesh];
  assert(mesh.primitives.length === 1, semanticName + " mesh primitive count changed");
  const primitive = mesh.primitives[0];
  assert((primitive.mode ?? 4) === 4, semanticName + " is not triangles");
  assert(primitive.indices !== undefined, semanticName + " is not indexed");
  const expectedSemantics = ["POSITION", "TANGENT", "NORMAL", "COLOR_0", "TEXCOORD_0"];
  assert(
    JSON.stringify(Object.keys(primitive.attributes)) === JSON.stringify(expectedSemantics),
    semanticName + " attribute layout changed",
  );
  const layouts = Object.fromEntries(
    Object.entries(primitive.attributes).map(([name, index]) => [
      name,
      accessorLayout(source, index),
    ]),
  );
  const indexLayout = accessorLayout(source, primitive.indices);
  const vertexCount = layouts.POSITION.accessor.count;
  for (const [name, layout] of Object.entries(layouts)) {
    assert(layout.accessor.componentType === FLOAT, semanticName + " " + name + " is not FLOAT");
    assert(layout.accessor.count === vertexCount, semanticName + " attribute counts differ");
  }
  assert(indexLayout.accessor.count % 3 === 0, semanticName + " has partial triangles");

  const transform = nodeTransform(node);
  const worldY = new Float64Array(vertexCount);
  const sourceBounds = emptyBounds();
  const local = [0, 0, 0];
  for (let vertex = 0; vertex < vertexCount; ++vertex) {
    const world = transformPosition(
      transform,
      readFloatVector(source, layouts.POSITION, vertex, local),
    );
    worldY[vertex] = world[1];
    addBounds(sourceBounds, world);
  }

  const union = makeUnionFind(vertexCount);
  for (let index = 0; index < indexLayout.accessor.count; index += 3) {
    const a = readIndex(source, indexLayout, index);
    const b = readIndex(source, indexLayout, index + 1);
    const c = readIndex(source, indexLayout, index + 2);
    assert(a < vertexCount && b < vertexCount && c < vertexCount, "Ivy index is out of range");
    union.unite(a, b);
    union.unite(a, c);
  }

  const minimumY = new Float64Array(vertexCount);
  const maximumY = new Float64Array(vertexCount);
  const triangleCounts = new Uint32Array(vertexCount);
  minimumY.fill(Infinity);
  maximumY.fill(-Infinity);
  for (let vertex = 0; vertex < vertexCount; ++vertex) {
    const root = union.find(vertex);
    minimumY[root] = Math.min(minimumY[root], worldY[vertex]);
    maximumY[root] = Math.max(maximumY[root], worldY[vertex]);
  }
  for (let index = 0; index < indexLayout.accessor.count; index += 3) {
    ++triangleCounts[union.find(readIndex(source, indexLayout, index))];
  }

  const summary = {
    total: { components: 0, vertices: 0, triangles: 0 },
    retained: { components: 0, vertices: 0, triangles: 0 },
    whollyBelowExactSeam: { components: 0, vertices: 0, triangles: 0 },
    whollyAboveExactSeam: { components: 0, vertices: 0, triangles: 0 },
    straddlingExactSeam: { components: 0, vertices: 0, triangles: 0 },
    removed: { components: 0, vertices: 0, triangles: 0 },
  };
  const retainedComponents = [];
  const rootRetained = new Uint8Array(vertexCount);
  let highestRetainedWorldY = -Infinity;
  let nearestRemovedWorldY = Infinity;
  for (let root = 0; root < vertexCount; ++root) {
    if (union.parents[root] >= 0) {
      continue;
    }
    const vertices = -union.parents[root];
    const triangles = triangleCounts[root];
    const exactClass =
      maximumY[root] <= TRIM_WORLD_Y
        ? "whollyBelowExactSeam"
        : minimumY[root] >= TRIM_WORLD_Y
          ? "whollyAboveExactSeam"
          : "straddlingExactSeam";
    for (const target of [summary.total, summary[exactClass]]) {
      ++target.components;
      target.vertices += vertices;
      target.triangles += triangles;
    }
    if (maximumY[root] <= KEEP_MAX_WORLD_Y) {
      rootRetained[root] = 1;
      ++summary.retained.components;
      summary.retained.vertices += vertices;
      summary.retained.triangles += triangles;
      highestRetainedWorldY = Math.max(highestRetainedWorldY, maximumY[root]);
      retainedComponents.push({
        root,
        vertices,
        triangles,
        byteCost: vertices * 64 + triangles * 6,
        minimumY: minimumY[root],
        maximumY: maximumY[root],
      });
    } else {
      ++summary.removed.components;
      summary.removed.vertices += vertices;
      summary.removed.triangles += triangles;
      nearestRemovedWorldY = Math.min(nearestRemovedWorldY, minimumY[root]);
    }
  }
  assert(
    summary.total.vertices === vertexCount &&
      summary.total.triangles === indexLayout.accessor.count / 3,
    semanticName + " component accounting failed",
  );
  assert(
    summary.retained.vertices + summary.removed.vertices === vertexCount,
    semanticName + " trim accounting failed",
  );

  return {
    semanticName,
    nodeIndex,
    node,
    mesh,
    primitive,
    layouts,
    indexLayout,
    transform,
    worldY,
    union,
    rootRetained,
    retainedComponents,
    sourceBounds,
    summary,
    highestRetainedWorldY,
    nearestRemovedWorldY,
  };
}

function assignBalancedParts(analyses, partCount) {
  assert(partCount > 0 && partCount < 127, "Invalid ivy part count");
  const bins = Array.from({ length: partCount }, (_, index) => ({
    index,
    bytes: 0,
    vertices: 0,
    triangles: 0,
    components: 0,
    materials: Object.fromEntries(
      analyses.map((analysis) => [
        analysis.semanticName,
        { bytes: 0, vertices: 0, triangles: 0, components: 0 },
      ]),
    ),
  }));
  const rootParts = Object.fromEntries(
    analyses.map((analysis) => {
      const rootPart = new Int8Array(analysis.union.parents.length);
      rootPart.fill(-1);
      return [analysis.semanticName, rootPart];
    }),
  );
  const components = analyses
    .flatMap((analysis, analysisIndex) =>
      analysis.retainedComponents.map((component) => ({
        ...component,
        analysisIndex,
        semanticName: analysis.semanticName,
      })),
    )
    .sort(
      (left, right) =>
        right.byteCost - left.byteCost ||
        left.analysisIndex - right.analysisIndex ||
        left.root - right.root,
    );
  for (const component of components) {
    bins.sort((left, right) => left.bytes - right.bytes || left.index - right.index);
    const bin = bins[0];
    rootParts[component.semanticName][component.root] = bin.index;
    bin.bytes += component.byteCost;
    bin.vertices += component.vertices;
    bin.triangles += component.triangles;
    ++bin.components;
    const materialBin = bin.materials[component.semanticName];
    materialBin.bytes += component.byteCost;
    materialBin.vertices += component.vertices;
    materialBin.triangles += component.triangles;
    ++materialBin.components;
  }
  bins.sort((left, right) => left.index - right.index);
  return { bins, rootParts };
}

async function loadSharp() {
  try {
    return (await import("sharp")).default;
  } catch {
    const nodeRoot = path.resolve(path.dirname(process.execPath), "..");
    const directCandidates = [
      path.join(path.dirname(process.execPath), "node_modules", "sharp", "lib", "index.js"),
      path.join(nodeRoot, "node_modules", "sharp", "lib", "index.js"),
    ];
    for (const modulePath of directCandidates) {
      if (existsSync(modulePath)) {
        try {
          return (await import(pathToFileURL(modulePath).href)).default;
        } catch {
          // Try the pnpm-resolved copy below; it retains Sharp's sibling dependencies.
        }
      }
    }
    const pnpmRoot = path.join(nodeRoot, "node_modules", ".pnpm");
    if (existsSync(pnpmRoot)) {
      const candidates = readdirSync(pnpmRoot)
        .filter((name) => name.startsWith("sharp@"))
        .sort()
        .reverse();
      for (const candidate of candidates) {
        const modulePath = path.join(
          pnpmRoot,
          candidate,
          "node_modules",
          "sharp",
          "lib",
          "index.js",
        );
        if (existsSync(modulePath)) {
          return (await import(pathToFileURL(modulePath).href)).default;
        }
      }
    }
    throw new Error(
      "The ivy importer requires sharp for deterministic 1024px texture resizing. " +
        "Install sharp or run with the Codex bundled Node runtime.",
    );
  }
}

async function resizeTextures(sourceDir) {
  const sharp = await loadSharp();
  sharp.cache(false);
  sharp.concurrency(1);
  const result = [];
  for (const descriptor of [
    { key: "normal", name: "IvyLeaf_Normal.png", renormalize: true },
    { key: "baseColor", name: "IvyLeaf_BaseColor.png", renormalize: false },
    { key: "roughness", name: "IvyLeaf_Roughness0.png", renormalize: false },
  ]) {
    const inputPath = expectedFile(sourceDir, sourceFiles[descriptor.key]);
    const input = readFileSync(inputPath);
    const sourceMetadata = await sharp(input, { limitInputPixels: false }).metadata();
    let pipeline = sharp(input, { limitInputPixels: false }).resize(
      TEXTURE_SIZE,
      TEXTURE_SIZE,
      { kernel: sharp.kernel.lanczos3 },
    );
    if (descriptor.renormalize) {
      const decoded = await pipeline.removeAlpha().raw().toBuffer({ resolveWithObject: true });
      assert(decoded.info.channels === 3, "Normal texture did not decode as RGB");
      for (let offset = 0; offset < decoded.data.length; offset += 3) {
        const x = decoded.data[offset] / 127.5 - 1;
        const y = decoded.data[offset + 1] / 127.5 - 1;
        const z = decoded.data[offset + 2] / 127.5 - 1;
        const length = Math.hypot(x, y, z) || 1;
        decoded.data[offset] = Math.round((x / length * 0.5 + 0.5) * 255);
        decoded.data[offset + 1] = Math.round((y / length * 0.5 + 0.5) * 255);
        decoded.data[offset + 2] = Math.round((z / length * 0.5 + 0.5) * 255);
      }
      pipeline = sharp(decoded.data, { raw: decoded.info });
    }
    const output = await pipeline
      .png({ compressionLevel: 9, adaptiveFiltering: false, effort: 10, palette: false })
      .toBuffer();
    const outputMetadata = await sharp(output, { limitInputPixels: false }).metadata();
    result.push({
      ...descriptor,
      inputPath,
      input,
      output,
      sourceMetadata: {
        width: sourceMetadata.width,
        height: sourceMetadata.height,
        channels: sourceMetadata.channels,
        hasAlpha: sourceMetadata.hasAlpha,
      },
      outputMetadata: {
        width: outputMetadata.width,
        height: outputMetadata.height,
        channels: outputMetadata.channels,
        hasAlpha: outputMetadata.hasAlpha,
      },
      sourceSha256: sha256(input),
      outputSha256: sha256(output),
    });
  }
  return {
    sharpVersion: sharp.versions.sharp,
    libvipsVersion: sharp.versions.vips,
    images: result,
  };
}

function writeFloatVector(buffer, byteOffset, values) {
  for (let component = 0; component < values.length; ++component) {
    buffer.writeFloatLE(Math.fround(values[component]), byteOffset + component * 4);
  }
}

function buildGeometryChunks(source, analysis, rootPart, partIndex) {
  const selected = analysis.retainedComponents
    .filter((component) => rootPart[component.root] === partIndex)
    .sort((left, right) => left.root - right.root);
  assert(selected.length > 0, analysis.semanticName + " output part is empty");

  const chunkDefinitions = [];
  let current = null;
  for (const component of selected) {
    assert(
      component.vertices <= MAX_UNSIGNED_SHORT_VERTICES,
      analysis.semanticName + " component cannot fit in a U16 primitive",
    );
    if (
      current === null ||
      current.vertexCount + component.vertices > MAX_UNSIGNED_SHORT_VERTICES
    ) {
      current = { components: [], vertexCount: 0, triangleCount: 0 };
      chunkDefinitions.push(current);
    }
    current.components.push(component);
    current.vertexCount += component.vertices;
    current.triangleCount += component.triangles;
  }

  const chunks = chunkDefinitions.map((definition, index) => ({
    semanticName: analysis.semanticName,
    index,
    componentCount: definition.components.length,
    vertexCount: definition.vertexCount,
    triangleCount: definition.triangleCount,
    indices: Buffer.allocUnsafe(definition.triangleCount * 3 * 2),
    positions: Buffer.allocUnsafe(definition.vertexCount * 3 * 4),
    tangents: Buffer.allocUnsafe(definition.vertexCount * 4 * 4),
    normals: Buffer.allocUnsafe(definition.vertexCount * 3 * 4),
    colors: Buffer.allocUnsafe(definition.vertexCount * 4 * 4),
    texcoords: Buffer.allocUnsafe(definition.vertexCount * 2 * 4),
    bounds: emptyBounds(),
    writtenVertices: 0,
    writtenIndices: 0,
  }));
  const rootChunk = new Int16Array(analysis.union.parents.length);
  rootChunk.fill(-1);
  for (let chunkIndex = 0; chunkIndex < chunkDefinitions.length; ++chunkIndex) {
    for (const component of chunkDefinitions[chunkIndex].components) {
      rootChunk[component.root] = chunkIndex;
    }
  }

  const remap = new Int32Array(analysis.union.parents.length);
  remap.fill(-1);
  const bounds = emptyBounds();
  const localPosition = [0, 0, 0];
  const localTangent = [0, 0, 0, 0];
  const localNormal = [0, 0, 0];
  const localColor = [0, 0, 0, 0];
  const localUv = [0, 0];
  for (let vertex = 0; vertex < remap.length; ++vertex) {
    const root = analysis.union.find(vertex);
    const chunkIndex = rootChunk[root];
    if (chunkIndex < 0) {
      continue;
    }
    const chunk = chunks[chunkIndex];
    const outputVertex = chunk.writtenVertices;
    remap[vertex] = outputVertex;
    const position = transformPosition(
      analysis.transform,
      readFloatVector(source, analysis.layouts.POSITION, vertex, localPosition),
    );
    const normal = transformDirection(
      analysis.transform,
      readFloatVector(source, analysis.layouts.NORMAL, vertex, localNormal),
    );
    readFloatVector(source, analysis.layouts.TANGENT, vertex, localTangent);
    const tangentDirection = transformDirection(
      analysis.transform,
      localTangent.slice(0, 3),
    );
    const tangent = [...tangentDirection, localTangent[3] < 0 ? -1 : 1];
    const color = readFloatVector(source, analysis.layouts.COLOR_0, vertex, localColor);
    const uv = readFloatVector(source, analysis.layouts.TEXCOORD_0, vertex, localUv);
    writeFloatVector(chunk.positions, outputVertex * 12, position);
    writeFloatVector(chunk.tangents, outputVertex * 16, tangent);
    writeFloatVector(chunk.normals, outputVertex * 12, normal);
    writeFloatVector(chunk.colors, outputVertex * 16, color);
    writeFloatVector(chunk.texcoords, outputVertex * 8, uv);
    const storedPosition = position.map(Math.fround);
    addBounds(chunk.bounds, storedPosition);
    addBounds(bounds, storedPosition);
    assert(
      position[1] <= KEEP_MAX_WORLD_Y + 1e-9,
      "Retained ivy vertex exceeds the trim predicate",
    );
    ++chunk.writtenVertices;
  }

  for (let index = 0; index < analysis.indexLayout.accessor.count; index += 3) {
    const sourceIndex = readIndex(source, analysis.indexLayout, index);
    const root = analysis.union.find(sourceIndex);
    const chunkIndex = rootChunk[root];
    if (chunkIndex < 0) {
      continue;
    }
    const chunk = chunks[chunkIndex];
    for (let corner = 0; corner < 3; ++corner) {
      const oldIndex = readIndex(source, analysis.indexLayout, index + corner);
      const newIndex = remap[oldIndex];
      assert(
        newIndex >= 0 && newIndex < chunk.vertexCount,
        "Ivy retained triangle references a removed vertex",
      );
      chunk.indices.writeUInt16LE(newIndex, chunk.writtenIndices * 2);
      ++chunk.writtenIndices;
    }
  }

  for (const chunk of chunks) {
    assert(
      chunk.writtenVertices === chunk.vertexCount &&
        chunk.writtenIndices === chunk.triangleCount * 3,
      analysis.semanticName + " primitive accounting changed",
    );
    delete chunk.writtenVertices;
    delete chunk.writtenIndices;
  }
  return {
    semanticName: analysis.semanticName,
    chunks,
    vertexCount: selected.reduce((total, component) => total + component.vertices, 0),
    triangleCount: selected.reduce((total, component) => total + component.triangles, 0),
    componentCount: selected.length,
    bounds,
  };
}

function appendBufferView(chunks, bufferViews, data, descriptor) {
  const currentLength = chunks.reduce((total, chunk) => total + chunk.length, 0);
  const padding = (4 - (currentLength % 4)) % 4;
  if (padding > 0) {
    chunks.push(Buffer.alloc(padding));
  }
  const byteOffset = currentLength + padding;
  const bufferView = {
    buffer: 0,
    byteOffset,
    byteLength: data.length,
    ...(descriptor.target === undefined ? {} : { target: descriptor.target }),
    name: descriptor.name,
  };
  const index = bufferViews.length;
  bufferViews.push(bufferView);
  chunks.push(data);
  return index;
}

function serializeGlb(json, bin) {
  const jsonBytes = Buffer.from(JSON.stringify(json));
  const paddedJsonLength = align4(jsonBytes.length);
  const totalLength = 12 + 8 + paddedJsonLength + 8 + bin.length;
  const output = Buffer.allocUnsafe(totalLength);
  output.writeUInt32LE(0x46546c67, 0);
  output.writeUInt32LE(2, 4);
  output.writeUInt32LE(totalLength, 8);
  output.writeUInt32LE(paddedJsonLength, 12);
  output.writeUInt32LE(0x4e4f534a, 16);
  jsonBytes.copy(output, 20);
  output.fill(0x20, 20 + jsonBytes.length, 20 + paddedJsonLength);
  const binHeader = 20 + paddedJsonLength;
  output.writeUInt32LE(bin.length, binHeader);
  output.writeUInt32LE(0x004e4942, binHeader + 4);
  bin.copy(output, binHeader + 8);
  return output;
}

function buildGlb(source, analyses, rootParts, partIndex, name, textures) {
  const geometries = analyses.map((analysis) =>
    buildGeometryChunks(
      source,
      analysis,
      rootParts[analysis.semanticName],
      partIndex,
    ),
  );
  const chunks = [];
  const bufferViews = [];
  const accessors = [];
  const appendPrimitive = (geometry, chunk, materialIndex) => {
    const prefix = `${geometry.semanticName}_${chunk.index + 1}`;
    const indexView = appendBufferView(chunks, bufferViews, chunk.indices, {
      target: ELEMENT_ARRAY_BUFFER,
      name: prefix + "_indices",
    });
    const positionView = appendBufferView(chunks, bufferViews, chunk.positions, {
      target: ARRAY_BUFFER,
      name: prefix + "_positions",
    });
    const tangentView = appendBufferView(chunks, bufferViews, chunk.tangents, {
      target: ARRAY_BUFFER,
      name: prefix + "_tangents",
    });
    const normalView = appendBufferView(chunks, bufferViews, chunk.normals, {
      target: ARRAY_BUFFER,
      name: prefix + "_normals",
    });
    const colorView = appendBufferView(chunks, bufferViews, chunk.colors, {
      target: ARRAY_BUFFER,
      name: prefix + "_colors",
    });
    const uvView = appendBufferView(chunks, bufferViews, chunk.texcoords, {
      target: ARRAY_BUFFER,
      name: prefix + "_texcoords",
    });
    const firstAccessor = accessors.length;
    accessors.push(
      {
        bufferView: indexView,
        componentType: UNSIGNED_SHORT,
        count: chunk.triangleCount * 3,
        type: "SCALAR",
        min: [0],
        max: [chunk.vertexCount - 1],
        name: prefix + "_indices",
      },
      {
        bufferView: positionView,
        componentType: FLOAT,
        count: chunk.vertexCount,
        type: "VEC3",
        min: chunk.bounds.min,
        max: chunk.bounds.max,
        name: prefix + "_positions",
      },
      {
        bufferView: tangentView,
        componentType: FLOAT,
        count: chunk.vertexCount,
        type: "VEC4",
        name: prefix + "_tangents",
      },
      {
        bufferView: normalView,
        componentType: FLOAT,
        count: chunk.vertexCount,
        type: "VEC3",
        name: prefix + "_normals",
      },
      {
        bufferView: colorView,
        componentType: FLOAT,
        count: chunk.vertexCount,
        type: "VEC4",
        name: prefix + "_colors",
      },
      {
        bufferView: uvView,
        componentType: FLOAT,
        count: chunk.vertexCount,
        type: "VEC2",
        name: prefix + "_texcoords",
      },
    );
    return {
      attributes: {
        POSITION: firstAccessor + 1,
        TANGENT: firstAccessor + 2,
        NORMAL: firstAccessor + 3,
        COLOR_0: firstAccessor + 4,
        TEXCOORD_0: firstAccessor + 5,
      },
      indices: firstAccessor,
      material: materialIndex,
      mode: 4,
    };
  };
  const meshPrimitives = geometries.map((geometry, materialIndex) =>
    geometry.chunks.map((chunk) => appendPrimitive(geometry, chunk, materialIndex)),
  );

  const images = [];
  const textureObjects = [];
  const samplers = [];
  samplers.push(structuredClone(source.json.samplers[0]));
  for (const texture of textures.images) {
    const view = appendBufferView(chunks, bufferViews, texture.output, {
      name: texture.name,
    });
    images.push({ bufferView: view, mimeType: "image/png", name: texture.name });
  }
  textureObjects.push(
    { sampler: 0, source: 0, name: "IvyLeaf_Normal.png" },
    { sampler: 0, source: 1, name: "IvyLeaf_BaseColor.png" },
    { sampler: 0, source: 2, name: "IvyLeaf_Roughness0.png" },
  );
  const finalPadding = (4 - (chunks.reduce((total, chunk) => total + chunk.length, 0) % 4)) % 4;
  if (finalPadding > 0) {
    chunks.push(Buffer.alloc(finalPadding));
  }
  const bin = Buffer.concat(chunks);

  const bounds = emptyBounds();
  for (const geometry of geometries) {
    addBounds(bounds, geometry.bounds.min);
    addBounds(bounds, geometry.bounds.max);
  }
  const componentCountForPart = geometries.reduce(
    (total, geometry) => total + geometry.componentCount,
    0,
  );
  const json = {
    asset: {
      version: "2.0",
      generator: "UVSR deterministic Intel Sponza ivy importer",
      extras: {
        uvsrImport: {
          source: sourceFiles.gltf,
          trimWorldY: TRIM_WORLD_Y,
           trimEpsilonMeters: TRIM_EPSILON_METERS,
           retention: "whole-indexed-components-with-max-world-y-below-threshold",
           bakedSourceNodeTransform: true,
           part: partIndex + 1,
           components: componentCountForPart,
           materials: Object.fromEntries(
             geometries.map((geometry) => [
               geometry.semanticName,
               {
                 components: geometry.componentCount,
                 primitives: geometry.chunks.length,
               },
             ]),
           ),
         },
       },
     },
     scene: 0,
     scenes: [{ nodes: [0, 1], name }],
     nodes: [
       { mesh: 0, name: name + " Leaves" },
       { mesh: 1, name: name + " Lianas" },
     ],
     meshes: [
       { name: name + " Leaves", primitives: meshPrimitives[0] },
       { name: name + " Lianas", primitives: meshPrimitives[1] },
     ],
     accessors,
     bufferViews,
     buffers: [{ byteLength: bin.length }],
     materials: source.json.materials.map((material) => structuredClone(material)),
     textures: textureObjects,
     images,
     samplers,
   };
  const output = serializeGlb(json, bin);
  assert(output.length < GITHUB_FILE_LIMIT, name + " exceeds GitHub's 100 MB limit");
  return {
    output,
    binBytes: bin.length,
    geometry: {
      componentCount: componentCountForPart,
      vertexCount: geometries.reduce((total, geometry) => total + geometry.vertexCount, 0),
      triangleCount: geometries.reduce((total, geometry) => total + geometry.triangleCount, 0),
      primitiveCount: geometries.reduce((total, geometry) => total + geometry.chunks.length, 0),
      bounds,
      materials: Object.fromEntries(
        geometries.map((geometry) => [
          geometry.semanticName,
          {
            components: geometry.componentCount,
            vertices: geometry.vertexCount,
            triangles: geometry.triangleCount,
            primitives: geometry.chunks.length,
            boundsWorld: geometry.bounds,
          },
        ]),
      ),
    },
    resources: {
      nodes: 2,
      meshes: 2,
      materials: 2,
      textures: textureObjects.length,
      images: images.length,
      samplers: samplers.length,
      accessors: accessors.length,
      bufferViews: bufferViews.length,
      buffers: 1,
    },
  };
}

function sourceEntry(filePath, sourceDir) {
  const data = readFileSync(filePath);
  return {
    path: path.relative(sourceDir, filePath).replaceAll("\\", "/"),
    byteLength: data.length,
    sha256: sha256(data),
  };
}

function verifyOrWrite(filePath, data, check) {
  if (check) {
    assert(existsSync(filePath), "Missing generated ivy output " + relative(filePath));
    const current = readFileSync(filePath);
    assert(
      current.equals(data),
      "Generated ivy output is stale: " + relative(filePath),
    );
  } else {
    mkdirSync(path.dirname(filePath), { recursive: true });
    writeFileSync(filePath, data);
  }
}

function verifyExistingOutputsFromReport() {
  assert(existsSync(reportPath), "Missing generated ivy report " + relative(reportPath));
  const report = JSON.parse(readFileSync(reportPath, "utf8"));
  assert(report.importer === "tools/import_sponza_ivy.mjs", "Unexpected ivy importer report");
  assert(report.deterministic === true, "Ivy report is not marked deterministic");
  const expectedPaths = Object.values(outputNames)
    .map((name) => relative(path.join(outputDir, name)))
    .sort();
  const reportedPaths = report.outputs.map((output) => output.path).sort();
  assert(
    JSON.stringify(reportedPaths) === JSON.stringify(expectedPaths),
    "Ivy report output set changed",
  );

  for (const output of report.outputs) {
    const filePath = path.resolve(repoRoot, output.path);
    assert(
      path.dirname(filePath) === path.resolve(outputDir),
      "Ivy report references an output outside its component directory",
    );
    assert(existsSync(filePath), "Missing generated ivy output " + output.path);
    const data = readFileSync(filePath);
    assert(data.length === output.byteLength, "Ivy output length differs from its report");
    assert(sha256(data) === output.sha256, "Ivy output hash differs from its report");
    assert(data.length < GITHUB_FILE_LIMIT, "Ivy output exceeds GitHub's 100 MB limit");
    assert(data.readUInt32LE(0) === 0x46546c67, "Ivy output is not GLB");
    assert(data.readUInt32LE(4) === 2, "Ivy output is not GLB 2.0");
    assert(data.readUInt32LE(8) === data.length, "Ivy GLB length field changed");
    const jsonLength = data.readUInt32LE(12);
    assert(data.readUInt32LE(16) === 0x4e4f534a, "Ivy GLB JSON chunk changed");
    const json = JSON.parse(data.subarray(20, 20 + jsonLength).toString("utf8"));
    const binHeader = 20 + jsonLength;
    const binLength = data.readUInt32LE(binHeader);
    assert(data.readUInt32LE(binHeader + 4) === 0x004e4942, "Ivy GLB BIN chunk changed");
    assert(binHeader + 8 + binLength === data.length, "Ivy GLB BIN length changed");
    assert(
      json.buffers.length === 1 &&
        json.buffers[0].uri === undefined &&
        json.buffers[0].byteLength === binLength,
      "Ivy GLB buffer is not self-contained",
    );
    assert(
      json.images.every((image) => image.uri === undefined && image.bufferView !== undefined),
      "Ivy GLB image is not embedded",
    );
    for (const [resource, count] of Object.entries(output.resources)) {
      assert(
        (json[resource]?.length ?? 0) === count,
        `Ivy GLB ${resource} count differs from its report`,
      );
    }
    const leafMaterial = json.materials.find((material) => material.name === "IvyLeaf");
    assert(
      leafMaterial?.doubleSided === true && leafMaterial.alphaMode === undefined,
      "Ivy leaf material domain differs from the audited source",
    );
    const baseColorImage = json.images.find(
      (image) => image.name === "IvyLeaf_BaseColor.png",
    );
    assert(baseColorImage !== undefined, "Ivy base-color image is missing");
    const bufferView = json.bufferViews[baseColorImage.bufferView];
    const pngOffset = binHeader + 8 + (bufferView.byteOffset ?? 0);
    assert(data.toString("ascii", pngOffset + 12, pngOffset + 16) === "IHDR", "Bad ivy PNG");
    assert(
      data.readUInt32BE(pngOffset + 16) === TEXTURE_SIZE &&
        data.readUInt32BE(pngOffset + 20) === TEXTURE_SIZE &&
        data[pngOffset + 25] === 2,
      "Ivy base-color PNG dimensions or RGB alpha domain changed",
    );
  }
  for (const name of obsoleteOutputNames) {
    assert(
      !existsSync(path.join(outputDir, name)),
      "Obsolete generated ivy output remains: " + name,
    );
  }
  return report;
}

async function generate(sourceDir) {
  const gltfPath = expectedFile(sourceDir, sourceFiles.gltf);
  const binPath = expectedFile(sourceDir, sourceFiles.bin);
  const gltfBuffer = readFileSync(gltfPath);
  const json = JSON.parse(gltfBuffer.toString("utf8"));
  assert(json.asset?.version === "2.0", "Ivy source is not glTF 2.0");
  assert(json.buffers?.length === 1, "Ivy source buffer layout changed");
  assert(json.buffers[0].uri === sourceFiles.bin, "Ivy source BIN URI changed");
  const bin = readFileSync(binPath);
  assert(bin.length === json.buffers[0].byteLength, "Ivy source BIN length changed");
  const source = {
    json,
    bin,
    view: new DataView(bin.buffer, bin.byteOffset, bin.byteLength),
  };
  assert(json.nodes.length === 2 && json.meshes.length === 2, "Ivy node layout changed");
  assert(json.nodes[0].name === "IvySim_Leaves", "Ivy leaves node changed");
  assert(json.nodes[1].name === "IvySim_Lianas", "Ivy lianas node changed");

  const textures = await resizeTextures(sourceDir);
  const baseColorTexture = textures.images.find((texture) => texture.key === "baseColor");
  assert(baseColorTexture !== undefined, "Ivy base-color texture audit failed");
  assert(
    baseColorTexture.sourceMetadata.channels === 3 &&
      baseColorTexture.sourceMetadata.hasAlpha === false &&
      baseColorTexture.outputMetadata.hasAlpha === false,
    "Ivy base-color alpha domain changed; audit alphaMode before importing",
  );
  assert(json.materials[0].doubleSided === true, "Ivy leaf doubleSided changed");
  assert(
    (json.materials[0].alphaMode ?? "OPAQUE") === "OPAQUE",
    "Ivy leaf alphaMode changed",
  );
  const leaves = analyzePrimitive(source, 0, "leaves");
  const lianas = analyzePrimitive(source, 1, "lianas");
  assert(leaves.summary.total.components === 38_575, "Ivy leaf component count changed");
  assert(lianas.summary.total.components === 5_231, "Ivy liana component count changed");
  assert(leaves.summary.retained.components === 27_884, "Retained leaf components changed");
  assert(lianas.summary.retained.components === 3_720, "Retained liana components changed");

  const analyses = [leaves, lianas];
  const assignment = assignBalancedParts(analyses, 2);
  for (const bin of assignment.bins) {
    assert(bin.materials.leaves.components > 0, "Balanced part has no ivy leaves");
    assert(bin.materials.lianas.components > 0, "Balanced part has no ivy lianas");
  }
  const outputs = [
    {
      key: "part1",
      name: "Intel PBR Sponza Ivy Part A",
      partIndex: 0,
    },
    {
      key: "part2",
      name: "Intel PBR Sponza Ivy Part B",
      partIndex: 1,
    },
  ];

  const outputReports = [];
  const outputBuffers = new Map();
  for (const descriptor of outputs) {
    const built = buildGlb(
      source,
      analyses,
      assignment.rootParts,
      descriptor.partIndex,
      descriptor.name,
      textures,
    );
    const filePath = path.join(outputDir, outputNames[descriptor.key]);
    outputBuffers.set(filePath, built.output);
    outputReports.push({
      path: relative(filePath),
      byteLength: built.output.length,
      sha256: sha256(built.output),
      belowGitHub100MBLimit: built.output.length < GITHUB_FILE_LIMIT,
      components: built.geometry.componentCount,
      vertices: built.geometry.vertexCount,
      triangles: built.geometry.triangleCount,
      primitives: built.geometry.primitiveCount,
      boundsWorld: built.geometry.bounds,
      materials: built.geometry.materials,
      binByteLength: built.binBytes,
      resources: built.resources,
    });
  }

  const report = {
    schemaVersion: 1,
    importer: "tools/import_sponza_ivy.mjs",
    deterministic: true,
    sourcePackage: "Intel Sponza pkg_b_ivy",
    sources: [
      sourceEntry(gltfPath, sourceDir),
      sourceEntry(binPath, sourceDir),
      sourceEntry(expectedFile(sourceDir, sourceFiles.normal), sourceDir),
      sourceEntry(expectedFile(sourceDir, sourceFiles.baseColor), sourceDir),
      sourceEntry(expectedFile(sourceDir, sourceFiles.roughness), sourceDir),
    ],
    trim: {
      seamWorldY: TRIM_WORLD_Y,
      epsilonMeters: TRIM_EPSILON_METERS,
      retainedMaximumWorldY: KEEP_MAX_WORLD_Y,
      predicate: "connectedComponent.maximumWorldY <= seamWorldY - epsilonMeters",
      positionsEvaluatedAfterFullSourceNodeTransform: true,
      partialComponentsRetained: 0,
    },
    textureResize: {
      width: TEXTURE_SIZE,
      height: TEXTURE_SIZE,
      kernel: "lanczos3",
      pngCompressionLevel: 9,
      adaptiveFiltering: false,
      normalVectorsRenormalized: true,
      sharpVersion: textures.sharpVersion,
      libvipsVersion: textures.libvipsVersion,
      images: textures.images.map((texture) => ({
        name: texture.name,
        sourceMetadata: texture.sourceMetadata,
        sourceByteLength: texture.input.length,
        sourceSha256: texture.sourceSha256,
        outputMetadata: texture.outputMetadata,
        outputByteLength: texture.output.length,
        outputSha256: texture.outputSha256,
      })),
    },
    materialAudit: {
      leafMaterial: json.materials[0].name,
      sourceAlphaModeDeclared: Object.hasOwn(json.materials[0], "alphaMode"),
      effectiveAlphaMode: json.materials[0].alphaMode ?? "OPAQUE",
      doubleSided: json.materials[0].doubleSided === true,
      baseColorTexture: "IvyLeaf_BaseColor.png",
      sourceBaseColorHasAlpha: textures.images.find(
        (texture) => texture.key === "baseColor",
      ).sourceMetadata.hasAlpha,
      sourceBaseColorChannels: textures.images.find(
        (texture) => texture.key === "baseColor",
      ).sourceMetadata.channels,
      outputBaseColorHasAlpha: textures.images.find(
        (texture) => texture.key === "baseColor",
      ).outputMetadata.hasAlpha,
      cutoutRepairApplied: false,
      conclusion:
        "The base-color PNG is RGB with no alpha channel, so glTF's default OPAQUE domain is correct; doubleSided remains true.",
    },
    sourceModel: {
      nodes: json.nodes.map((node) => ({
        name: node.name,
        rotation: node.rotation ?? [0, 0, 0, 1],
        scale: node.scale ?? [1, 1, 1],
        translation: node.translation ?? [0, 0, 0],
      })),
      resources: {
        nodes: json.nodes.length,
        meshes: json.meshes.length,
        materials: json.materials.length,
        textures: json.textures.length,
        images: json.images.length,
        samplers: json.samplers.length,
        accessors: json.accessors.length,
        bufferViews: json.bufferViews.length,
      },
      leaves: {
        boundsWorld: leaves.sourceBounds,
        components: leaves.summary,
        highestRetainedWorldY: leaves.highestRetainedWorldY,
        nearestRemovedWorldY: leaves.nearestRemovedWorldY,
      },
      lianas: {
        boundsWorld: lianas.sourceBounds,
        components: lianas.summary,
        highestRetainedWorldY: lianas.highestRetainedWorldY,
        nearestRemovedWorldY: lianas.nearestRemovedWorldY,
      },
    },
    partitioning: {
      strategy:
        "largest-component-first balance across both materials, then deterministic ascending-root U16 primitive packing",
      parts: assignment.bins,
      indexComponentType: "UNSIGNED_SHORT",
      maximumVerticesPerPrimitive: MAX_UNSIGNED_SHORT_VERTICES,
      minimumSelfContainedPartsAtPreservedFloat32Attributes: 2,
      minimumProof:
        "Retained float32 vertex attributes alone exceed 100,000,000 bytes, so one GLB is impossible; two embedded-texture GLBs are below the limit.",
    },
    outputs: outputReports,
  };
  const reportBuffer = Buffer.from(JSON.stringify(report, null, 2) + "\n");
  return { outputBuffers, reportBuffer, report };
}

async function main() {
  const argumentsList = process.argv.slice(2);
  const environmentSource = process.env.UVSR_SPONZA_IVY_SOURCE?.trim();
  let sourceDir = environmentSource ? path.resolve(environmentSource) : null;
  let check = false;
  for (let index = 0; index < argumentsList.length; ++index) {
    const argument = argumentsList[index];
    if (argument === "--check") {
      check = true;
    } else if (argument === "--source") {
      assert(index + 1 < argumentsList.length, "--source requires a directory");
      sourceDir = path.resolve(argumentsList[++index]);
    } else {
      throw new Error(
        "Usage: node tools/import_sponza_ivy.mjs --source <pkg_b_ivy> [--check]\n" +
          "       node tools/import_sponza_ivy.mjs --check\n" +
          "UVSR_SPONZA_IVY_SOURCE may be used instead of --source.",
      );
    }
  }
  if (sourceDir === null) {
    assert(
      check,
      "Ivy generation requires --source <pkg_b_ivy> or UVSR_SPONZA_IVY_SOURCE",
    );
    verifyExistingOutputsFromReport();
    console.log("Intel Sponza ivy outputs match import-report.json and are self-contained.");
    return;
  }
  assert(existsSync(sourceDir), "Ivy source directory does not exist: " + sourceDir);
  assert(statSync(sourceDir).isDirectory(), "Ivy source is not a directory: " + sourceDir);
  const generated = await generate(sourceDir);
  for (const name of obsoleteOutputNames) {
    const obsoletePath = path.join(outputDir, name);
    if (existsSync(obsoletePath)) {
      assert(!check, "Obsolete generated ivy output remains: " + relative(obsoletePath));
      unlinkSync(obsoletePath);
    }
  }
  for (const [filePath, data] of generated.outputBuffers) {
    verifyOrWrite(filePath, data, check);
  }
  verifyOrWrite(reportPath, generated.reportBuffer, check);
  if (check) {
    console.log("Intel Sponza ivy outputs are deterministic and current.");
  } else {
    for (const output of generated.report.outputs) {
      console.log(`Wrote ${output.path} (${output.byteLength.toLocaleString()} bytes)`);
    }
    console.log(`Wrote ${relative(reportPath)}`);
  }
}

const isMain =
  process.argv[1] !== undefined &&
  path.resolve(process.argv[1]).toLowerCase() === fileURLToPath(import.meta.url).toLowerCase();
if (isMain) {
  await main();
}
