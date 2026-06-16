import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

// Simple Vite app for the World-Sim UI design-system prototype.
export default defineConfig({
  plugins: [react()],
  server: {
    port: 5174,
    open: false,
  },
});
