<!-- $FreeBSD: src/release/doc/en_US.ISO8859-1/share/sgml/release.dsl,v 1.1.2.8.6.1 2005/01/21 11:20:40 hrs Exp $ -->

<!DOCTYPE style-sheet PUBLIC "-//James Clark//DTD DSSSL Style Sheet//EN" [
<!ENTITY release.dsl PUBLIC "-//FreeBSD//DOCUMENT Release Notes DocBook Language Neutral Stylesheet//EN" CDATA DSSSL>
<!ENTITY % output.html  "IGNORE"> 
<!ENTITY % output.print "IGNORE">
]>

<style-sheet>
  <style-specification use="docbook">
    <style-specification-body>
 
      <![ %output.html; [ 
        <!-- Generate links to HTML man pages -->
        (define %refentry-xref-link% #t)

	(define ($email-footer$)
          (make sequence
	    (make element gi: "p"
                  attributes: (list (list "align" "center"))
              (make element gi: "small"
                (literal "This file, and other release-related documents, can be downloaded from ")
		(create-link (list (list "HREF" (entity-text "release.url")))
                  (literal (entity-text "release.url")))
                (literal ".")))
            (make element gi: "p"
                  attributes: (list (list "align" "center"))
              (make element gi: "small"  
                (literal "For questions about FreeBSD, read the ")
		(create-link
		  (list (list "HREF" "http://www.FreeBSD.org/docs.html"))
                  (literal "documentation"))
                (literal " before contacting <")
		(create-link
		  (list (list "HREF" "mailto:questions@FreeBSD.org"))
                  (literal "questions@FreeBSD.org"))
                (literal ">.")

            (make element gi: "p"
                  attributes: (list (list "align" "center"))
	      (literal "For questions about this documentation, e-mail <")
	      (create-link (list (list "HREF" "mailto:doc@FreeBSD.org"))
                (literal "doc@FreeBSD.org"))
	      (literal ">."))))))
      ]]>

    </style-specification-body>
  </style-specification>

  <external-specification id="docbook" document="release.dsl">
</style-sheet>
