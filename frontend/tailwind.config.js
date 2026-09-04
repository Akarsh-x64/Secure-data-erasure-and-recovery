/** @type {import('tailwindcss').Config} */
module.exports = {
  content: [
    "./src/renderer/**/*.{js,ts,jsx,tsx,html}"
  ],
  theme: {
    extend: {
      colors: {
        background: {
          main: "var(--bg-main)",
          sidebar: "var(--bg-sidebar)",
          icon: "var(--bg-icon)",
        },
        ui: {
          selection: "var(--ui-selection)",
          outline: "var(--ui-outline)",
        },
        text: {
          pure: "var(--text-pure)",
          muted: "var(--text-muted)",
        },
        status: {
          valid: "var(--status-valid)",
          warning: "var(--status-warning)",
          error: "var(--status-error)",
        }
      },
      /* Enforces Sharp Edges globally */
      borderRadius: {
        DEFAULT: "0px", // Makes the standard `rounded` class sharp
        sm: "0.125rem", // 2px for minimal rounding when explicitly needed (rounded-sm)
      },
      /* Enforces explicit 1px structural boundaries */
      borderWidth: {
        DEFAULT: '1px',
      },
    },
  },
  plugins: [],
}