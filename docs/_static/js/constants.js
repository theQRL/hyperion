// Site URL
const SITE_URL = "https://docs.soliditylang.org"
const { origin, pathname } = location;
const pathSplit = pathname.split("/");
const rootPath = origin.includes(SITE_URL) && pathSplit.length > 3 ? pathSplit.splice(1, 2).join("/") : ''
const ROOT_URL = `${origin}/${rootPath}`;

// Color mode constants
const [DARK, LIGHT] = ["dark", "light"];
const LIGHT_LOGO_PATH = `${ROOT_URL}/_static/img/logo.svg`;
const DARK_LOGO_PATH = `${ROOT_URL}/_static/img/logo-dark.svg`;
const SUN_ICON_PATH = `${ROOT_URL}/_static/img/sun.svg`;
const MOON_ICON_PATH = `${ROOT_URL}/_static/img/moon.svg`;
const LIGHT_HAMBURGER_PATH = `${ROOT_URL}/_static/img/hamburger-light.svg`;
const DARK_HAMBURGER_PATH = `${ROOT_URL}/_static/img/hamburger-dark.svg`;
const COLOR_TOGGLE_ICON_CLASS = "color-toggle-icon";
const HYPERION_LOGO_CLASS = "hyperion-logo";
const LS_COLOR_SCHEME = "color-scheme";

// Hyperion navigation constants
const HYPERION_HOME_URL = "https://soliditylang.org";
const BLOG_URL = `${HYPERION_HOME_URL}/blog`;
const DOCS_URL = "/";
const USE_CASES_PATH = `${HYPERION_HOME_URL}/use-cases`;
const CONTRIBUTE_PATH = `/en/latest/contributing.html`;
const ABOUT_PATH = `${HYPERION_HOME_URL}/about`;
const FORUM_URL = "https://forum.soliditylang.org/";
const NAV_LINKS = [
  { name: "Blog", href: BLOG_URL },
  { name: "Documentation", href: DOCS_URL },
  { name: "Use cases", href: USE_CASES_PATH },
  { name: "Contribute", href: CONTRIBUTE_PATH },
  { name: "About", href: ABOUT_PATH },
  { name: "Forum", href: FORUM_URL },
];

const MOBILE_MENU_TOGGLE_CLASS = "shift";
const WRAPPER_CLASS = "unified-wrapper";
