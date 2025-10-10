import { GeistMono, GeistSans } from "geist/font"
import localFont from "next/font/local"

import { cn } from "@/lib/utils"

const fontSans = GeistSans

const fontMono = GeistMono

const fontWaldenburg = localFont({
  src: [
    {
      path: "../public/fonts/waldenburg/Waldenburg-Regular.woff2",
      weight: "400",
    },
    {
      path: "../public/fonts/waldenburg/Waldenburg-Bold.woff2",
      weight: "700",
    },
  ],
  variable: "--font-waldenburg",
})

const fontWaldenburgHF = localFont({
  src: [
    {
      path: "../public/fonts/waldenburg-semi-condensed/Waldenburg-Bold-SemiCondensed.woff2",
      weight: "700",
    },
  ],
  variable: "--font-waldenburg-ht",
  weight: "700",
})

export const fontVariables = cn(
  fontSans.variable,
  fontMono.variable,
  fontWaldenburg.variable,
  fontWaldenburgHF.variable
)
