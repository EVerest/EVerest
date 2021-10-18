
class RstCreator():
    def create_ref_anchor(self, identifier: str) -> str:
        result = ".. _" + identifier + ":\n\n"
        return result

    def create_toggle_header(self, header: str, shift) -> str:
        result = " " * shift + ".. toggle-header::\n"
        result += " " * shift + " :header: " + header + "\n\n"
        return result

    def write_bold(self, text: str) -> str:
        return "**" + text + "** "
    
    def write_italic(self, text: str) -> str:
        return "*" + text + "* "
        
    def create_title(self, title: str) -> str:
        return self.create_anytitle(title, "#")

    def create_subtitle(self, subtitle: str) -> str:
        return self.create_anytitle(subtitle, "*")

    def create_subsubtitle(self, subsubtitle: str) -> str:
        return self.create_anytitle(subsubtitle, "=")

    # help method for all titles
    def create_anytitle(self, anytitle: str, symbol: str) -> str:
        result = anytitle + "\n" + symbol * len(anytitle) + "\n\n"
        return result
