import { defineConfig } from 'astro/config';
import starlight from '@astrojs/starlight';

export default defineConfig({
  site: 'https://Ethiquema.github.io',
  base: '/ryu_ldn_nx',
  integrations: [
    starlight({
      title: 'ryu_ldn_nx',
      description: 'Nintendo Switch LDN sysmodule for online local multiplayer',
      head: [],
      social: [
        { icon: 'github', label: 'GitHub', href: 'https://github.com/Ethiquema/ryu_ldn_nx' },
      ],
      sidebar: [
        {
          label: 'Getting Started',
          items: [
            { label: 'Introduction', link: '/guides/introduction/' },
            { label: 'Installation', link: '/guides/installation/' },
            { label: 'Configuration', link: '/guides/configuration/' },
          ],
        },
        {
          label: 'Guides',
          items: [
            { label: 'Using the Overlay', link: '/guides/overlay/' },
            { label: 'Troubleshooting', link: '/guides/troubleshooting/' },
          ],
        },
        {
          label: 'Development',
          items: [
            { label: 'Contributing', link: '/guides/contributing/' },
            { label: 'Building from Source', link: '/guides/building/' },
            { label: 'Architecture', link: '/guides/architecture/' },
          ],
        },
        {
          label: 'API Reference',
          autogenerate: { directory: 'api' },
        },
        {
          label: 'Reference',
          items: [
            { label: 'IPC Commands', link: '/reference/ipc-commands/' },
            { label: 'Protocol', link: '/reference/protocol/' },
          ],
        },
      ],
    }),
  ],
});
