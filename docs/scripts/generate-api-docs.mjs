/**
 * Generate Starlight-compatible Markdown from Doxygen XML output
 *
 * This script parses Doxygen XML and generates .mdx files for each class/struct
 */

import { readFileSync, writeFileSync, readdirSync, existsSync, mkdirSync } from 'fs';
import { join, dirname } from 'path';
import { fileURLToPath } from 'url';
import { XMLParser } from 'fast-xml-parser';

const __dirname = dirname(fileURLToPath(import.meta.url));
const DOXYGEN_XML_DIR = join(__dirname, '../doxygen-xml');
const OUTPUT_DIR = join(__dirname, '../src/content/docs/api');
const SKIP_INDEX = true; // Don't overwrite hand-written index.mdx

const parser = new XMLParser({
  ignoreAttributes: false,
  attributeNamePrefix: '@_',
  textNodeName: '#text',
});

/**
 * Escape special MDX characters in text (but not in code blocks)
 */
function escapeMdx(text) {
  if (!text) return '';
  // Escape < and > outside of backticks
  // Also escape curly braces which are JSX expressions in MDX
  return text
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/\{/g, '\\{')
    .replace(/\}/g, '\\}');
}

/**
 * Extract text content from a Doxygen node
 */
function extractText(node) {
  if (!node) return '';
  if (typeof node === 'string') return node;
  if (node['#text']) return node['#text'];
  if (Array.isArray(node)) return node.map(extractText).join('');
  if (typeof node === 'object') {
    return Object.values(node).map(extractText).join('');
  }
  return String(node);
}

/**
 * Convert Doxygen description to Markdown
 */
function descriptionToMarkdown(desc) {
  if (!desc) return '';

  let md = extractText(desc);

  // Clean up common patterns
  md = md.replace(/\s+/g, ' ').trim();

  // Escape MDX special characters
  md = escapeMdx(md);

  // Remove Doxygen internal references
  md = md.replace(/classams_[a-zA-Z0-9_]+/g, '');
  md = md.replace(/autotoc_md\d+/g, '');

  return md;
}

/**
 * Generate Markdown for a class/struct
 */
function generateClassDoc(compound) {
  const name = compound.compoundname || 'Unknown';
  const kind = compound['@_kind'] || 'class';
  const briefDesc = descriptionToMarkdown(compound.briefdescription);
  const detailedDesc = descriptionToMarkdown(compound.detaileddescription);

  let md = `---
title: ${name}
description: ${briefDesc || `${kind} ${name}`}
---

# ${name}

${briefDesc}

${detailedDesc}

`;

  // Process sections (public members, etc.)
  const sections = compound.sectiondef;
  if (sections) {
    const sectionArray = Array.isArray(sections) ? sections : [sections];

    for (const section of sectionArray) {
      const sectionKind = section['@_kind'];
      const members = section.memberdef;

      if (!members) continue;

      const memberArray = Array.isArray(members) ? members : [members];

      // Group by kind
      const functions = memberArray.filter(m => m['@_kind'] === 'function');
      const variables = memberArray.filter(m => m['@_kind'] === 'variable');
      const enums = memberArray.filter(m => m['@_kind'] === 'enum');

      if (functions.length > 0 && sectionKind.includes('func')) {
        md += `## Methods\n\n`;
        for (const func of functions) {
          md += generateFunctionDoc(func);
        }
      }

      if (variables.length > 0 && sectionKind.includes('attrib')) {
        md += `## Members\n\n`;
        for (const v of variables) {
          md += generateVariableDoc(v);
        }
      }

      if (enums.length > 0) {
        md += `## Enumerations\n\n`;
        for (const e of enums) {
          md += generateEnumDoc(e);
        }
      }
    }
  }

  return md;
}

/**
 * Generate Markdown for a function/method
 */
function generateFunctionDoc(member) {
  const name = member.name || 'unknown';
  const returnType = extractText(member.type) || 'void';
  const briefDesc = descriptionToMarkdown(member.briefdescription);
  const detailedDesc = descriptionToMarkdown(member.detaileddescription);

  let md = `### \`${name}\`\n\n`;

  md += `\`\`\`cpp\n${returnType} ${name}(`;

  // Parameters
  const params = member.param;
  if (params) {
    const paramArray = Array.isArray(params) ? params : [params];
    const paramStrs = paramArray.map(p => {
      const type = extractText(p.type);
      const declname = p.declname || '';
      return `${type} ${declname}`.trim();
    });
    md += paramStrs.join(', ');
  }

  md += `)\n\`\`\`\n\n`;

  if (briefDesc) md += `${briefDesc}\n\n`;
  if (detailedDesc) md += `${detailedDesc}\n\n`;

  // Document parameters
  if (params) {
    const paramArray = Array.isArray(params) ? params : [params];
    if (paramArray.length > 0) {
      md += `**Parameters:**\n\n`;
      for (const p of paramArray) {
        const declname = p.declname || 'param';
        const type = escapeMdx(extractText(p.type));
        md += `- \`${declname}\` (\`${type}\`)\n`;
      }
      md += `\n`;
    }
  }

  // Return value
  if (returnType && returnType !== 'void') {
    md += `**Returns:** \`${returnType}\`\n\n`;
  }

  return md;
}

/**
 * Generate Markdown for a variable/member
 */
function generateVariableDoc(member) {
  const name = member.name || 'unknown';
  const type = extractText(member.type) || 'unknown';
  const briefDesc = descriptionToMarkdown(member.briefdescription);

  let md = `### \`${name}\`\n\n`;
  md += `**Type:** \`${type}\`\n\n`;
  if (briefDesc) md += `${briefDesc}\n\n`;

  return md;
}

/**
 * Generate Markdown for an enum
 */
function generateEnumDoc(member) {
  const name = member.name || 'unknown';
  const briefDesc = descriptionToMarkdown(member.briefdescription);

  let md = `### \`${name}\`\n\n`;
  if (briefDesc) md += `${briefDesc}\n\n`;

  const values = member.enumvalue;
  if (values) {
    const valueArray = Array.isArray(values) ? values : [values];
    md += `| Value | Description |\n|-------|-------------|\n`;
    for (const v of valueArray) {
      const vname = v.name || '';
      const vdesc = descriptionToMarkdown(v.briefdescription) || '';
      md += `| \`${vname}\` | ${vdesc} |\n`;
    }
    md += `\n`;
  }

  return md;
}

/**
 * Main function
 */
async function main() {
  console.log('Generating API documentation from Doxygen XML...');

  // Check if XML directory exists
  if (!existsSync(DOXYGEN_XML_DIR)) {
    console.log('Doxygen XML not found, creating placeholder API docs...');

    // Create output directory
    if (!existsSync(OUTPUT_DIR)) {
      mkdirSync(OUTPUT_DIR, { recursive: true });
    }

    // Create index page
    const indexContent = `---
title: API Reference
description: API documentation for ryu_ldn_nx
---

# API Reference

This section contains the API documentation generated from the source code.

## Modules

### Protocol
- [RyuProtocol](/ryu_ldn_nx/api/protocol) - Protocol encoder/decoder
- [PacketBuffer](/ryu_ldn_nx/api/packet-buffer) - Packet buffering utilities
- [Types](/ryu_ldn_nx/api/types) - Protocol types and structures

### Network
- [NetworkClient](/ryu_ldn_nx/api/network-client) - Main network client
- [TcpClient](/ryu_ldn_nx/api/tcp-client) - TCP socket wrapper
- [ConnectionState](/ryu_ldn_nx/api/connection-state) - Connection state machine
- [ReconnectManager](/ryu_ldn_nx/api/reconnect) - Automatic reconnection

### LDN Service
- [LdnMitMService](/ryu_ldn_nx/api/ldn-mitm-service) - MITM service implementation
- [LdnICommunication](/ryu_ldn_nx/api/ldn-icommunication) - IPC command handler
- [LdnStateMachine](/ryu_ldn_nx/api/ldn-state-machine) - LDN state management
- [LdnConfigService](/ryu_ldn_nx/api/ldn-config-service) - Overlay configuration IPC

### Config
- [Config](/ryu_ldn_nx/api/config) - Configuration parser
`;

    writeFileSync(join(OUTPUT_DIR, 'index.mdx'), indexContent);
    console.log('Created placeholder API index');
    return;
  }

  // Create output directory
  if (!existsSync(OUTPUT_DIR)) {
    mkdirSync(OUTPUT_DIR, { recursive: true });
  }

  // Read index.xml to get all compounds
  const indexPath = join(DOXYGEN_XML_DIR, 'index.xml');
  if (!existsSync(indexPath)) {
    console.error('index.xml not found in Doxygen XML directory');
    return;
  }

  const indexXml = readFileSync(indexPath, 'utf-8');
  const index = parser.parse(indexXml);

  const compounds = index.doxygenindex?.compound || [];
  const compoundArray = Array.isArray(compounds) ? compounds : [compounds];

  // Filter to classes and structs
  const classCompounds = compoundArray.filter(c =>
    c['@_kind'] === 'class' || c['@_kind'] === 'struct'
  );

  console.log(`Found ${classCompounds.length} classes/structs`);

  // Generate documentation for each class
  for (const compound of classCompounds) {
    const refid = compound['@_refid'];
    const name = compound.name;

    // Read the compound XML file
    const compoundPath = join(DOXYGEN_XML_DIR, `${refid}.xml`);
    if (!existsSync(compoundPath)) continue;

    try {
      const compoundXml = readFileSync(compoundPath, 'utf-8');
      const compoundData = parser.parse(compoundXml);
      const compoundDef = compoundData.doxygen?.compounddef;

      if (!compoundDef) continue;

      const markdown = generateClassDoc(compoundDef);

      // Create safe filename
      const safeName = name.replace(/::/g, '-').replace(/[<>]/g, '').toLowerCase();
      const outputPath = join(OUTPUT_DIR, `${safeName}.mdx`);

      writeFileSync(outputPath, markdown);
      console.log(`Generated: ${safeName}.mdx`);
    } catch (err) {
      console.error(`Error processing ${name}: ${err.message}`);
    }
  }

  // Generate index page
  const indexContent = `---
title: API Reference
description: API documentation for ryu_ldn_nx
---

# API Reference

This section contains the API documentation generated from the source code.

## Classes

${classCompounds.map(c => {
  const safeName = c.name.replace(/::/g, '-').replace(/[<>]/g, '').toLowerCase();
  return `- [${c.name}](/ryu_ldn_nx/api/${safeName})`;
}).join('\n')}
`;

  writeFileSync(join(OUTPUT_DIR, 'index.mdx'), indexContent);
  console.log('Generated API index');
}

main().catch(console.error);
