/* 
 * tkTableCell.c --
 *
 *	This module implements cell oriented functions for table
 *	widgets.
 *
 * Copyright (c) 1998-1999 Jeffrey Hobbs
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 */
 
#include "tkVMacro.h"
#include "tkTable.h"

static int	TableSortCompareProc _ANSI_ARGS_((CONST VOID *first,
						  CONST VOID *second));

/*
 *----------------------------------------------------------------------
 *
 * TableTrueCell --
 *	Takes a row,col pair in user coords and returns the true
 *	cell that it relates to, either dimension bounded, or a
 *	span cell if it was hidden.
 *
 * Results:
 *	The true row, col in user coords are placed in the pointers.
 *	If the value changed for some reasons, 0 is returned (it was not
 *	the /true/ cell).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
int
TableTrueCell(Table *tablePtr, int r, int c, int *row, int *col)
{
    *row = r; *col = c;
    /* We check spans before constraints, because we don't want to
     * constrain and then think we ended up in a span
     */
    if (tablePtr->spanAffTbl && !(tablePtr->flags & AVOID_SPANS)) {
	char buf[INDEX_BUFSIZE];
	Tcl_HashEntry *entryPtr;

	TableMakeArrayIndex(r, c, buf);
	entryPtr = Tcl_FindHashEntry(tablePtr->spanAffTbl, buf);
	if ((entryPtr != NULL) &&
	    ((char *)Tcl_GetHashValue(entryPtr) != NULL)) {
	    /* This cell is covered by another spanning cell */
	    /* We need to return the coords for that cell */
	    TableParseArrayIndex(row, col,
				 (char *)Tcl_GetHashValue(entryPtr));
	    return 0;
	}
    }
    *row = MIN(MAX(tablePtr->rowOffset,r),
	       tablePtr->rows-1+tablePtr->rowOffset);
    *col = MIN(MAX(tablePtr->colOffset,c),
	       tablePtr->cols-1+tablePtr->colOffset);
    return ((*row == r) && (*col == c));
}

/*
 *----------------------------------------------------------------------
 *
 * TableCellCoords --
 *	Takes a row,col pair in real coords and finds it position
 *	on the virtual screen.
 *
 * Results:
 *	The virtual x, y, width, and height of the cell
 *	are placed in the pointers.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
int
TableCellCoords(Table *tablePtr, int row, int col,
		int *x, int *y, int *w, int *h)
{
    register int hl = tablePtr->highlightWidth;
    int result = CELL_OK;

    if (tablePtr->rows <= 0 || tablePtr->cols <= 0) {
	*w = *h = *x = *y = 0;
	return CELL_BAD;
    }
    /* real coords required, always should be passed acceptable values,
   * but this is a possible seg fault otherwise */
#ifdef BOUNDS_CHECK
    row = MIN(tablePtr->rows-1, MAX(0, row));
    col = MIN(tablePtr->cols-1, MAX(0, col));
#endif
    *w = tablePtr->colPixels[col];
    *h = tablePtr->rowPixels[row];
    /* Adjust for sizes of spanning cells
   * and ensure that this cell isn't "hidden"
   */
    if (tablePtr->spanAffTbl && !(tablePtr->flags & AVOID_SPANS)) {
	char buf[INDEX_BUFSIZE];
	Tcl_HashEntry *entryPtr;

	TableMakeArrayIndex(row+tablePtr->rowOffset,
			    col+tablePtr->colOffset, buf);
	entryPtr = Tcl_FindHashEntry(tablePtr->spanAffTbl, buf);
	if (entryPtr != NULL) {
	    int rs, cs;
	    char *cell;

	    cell = (char *) Tcl_GetHashValue(entryPtr);
	    if (cell != NULL) {
		/* This cell is covered by another spanning cell */
		/* We need to return the coords for that cell */
		TableParseArrayIndex(&rs, &cs, cell);
		*w = rs;
		*h = cs;
		result = CELL_HIDDEN;
		goto setxy;
	    }
	    /* Get the actual span values out of spanTbl */
	    entryPtr = Tcl_FindHashEntry(tablePtr->spanTbl, buf);
	    cell = (char *) Tcl_GetHashValue(entryPtr);
	    TableParseArrayIndex(&rs, &cs, cell);
	    if (rs > 0) {
		if (row < tablePtr->titleRows) {
		    rs = MIN(tablePtr->titleRows-1-row, rs);
		} else {
		    rs = MIN(tablePtr->rows-1, row+rs);
		}
		*h = tablePtr->rowStarts[rs+1]-tablePtr->rowStarts[row];
		result = CELL_SPAN;
	    } else if (rs <= 0) {
		/* currently negative spans are not supported */
	    }
	    if (cs > 0) {
		if (col < tablePtr->titleCols) {
		    cs = MIN(tablePtr->titleCols-1-col, cs);
		} else {
		    cs = MIN(tablePtr->cols-1, col+cs);
		}
		*w = tablePtr->colStarts[cs+1]-tablePtr->colStarts[col];
		result = CELL_SPAN;
	    } else if (cs <= 0) {
		/* currently negative spans are not supported */
	    }
	}
    }
setxy:
    *x = hl + tablePtr->colStarts[col] - 
	((col < tablePtr->titleCols) ? 0 :
	 tablePtr->colStarts[tablePtr->leftCol]
	 - tablePtr->colStarts[tablePtr->titleCols]);
    *y = hl + tablePtr->rowStarts[row] -
	((row < tablePtr->titleRows) ? 0 :
	 tablePtr->rowStarts[tablePtr->topRow]
	 - tablePtr->rowStarts[tablePtr->titleRows]);
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * TableCellVCoords --
 *	Takes a row,col pair in real coords and finds it position
 *	on the actual screen.  The full arg specifies whether
 *	only 100% visible cells should be considered visible.
 *
 * Results:
 *	The x, y, width, and height of the cell are placed in the pointers,
 *	depending upon visibility of the cell.
 *	Returns 0 for hidden and 1 for visible cells.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
int
TableCellVCoords(Table *tablePtr, int row, int col,
		 int *rx, int *ry, int *rw, int *rh, int full)
{
    int x, y, w, h, w0, h0, cellType, hl = tablePtr->highlightWidth;

    if (tablePtr->tkwin == NULL) return 0;

    /* Necessary to use separate vars in case dummies are passed in */
    cellType = TableCellCoords(tablePtr, row, col, &x, &y, &w, &h);
    *rx = x; *ry = y; *rw = w; *rh = h;
    if (cellType == CELL_OK) {
	if ((row < tablePtr->topRow && row >= tablePtr->titleRows) ||
	    (col < tablePtr->leftCol && col >= tablePtr->titleCols)) {
	    /* non-spanning cell hiding in "dead" space
	     * between title areas and visible cells */
	    return 0;
	}
    } else if (cellType == CELL_SPAN) {
	/* we might need to treat full better is CELL_SPAN but primary
	 * cell is visible */
	int topX = tablePtr->colStarts[tablePtr->titleCols]+hl;
	int topY = tablePtr->rowStarts[tablePtr->titleRows]+hl;
	if (col < tablePtr->leftCol && col >= tablePtr->titleCols) {
	    if (full || (x+w < topX)) {
		return 0;
	    } else {
		w -= topX-x;
		x = topX;
	    }
	}
	if (row < tablePtr->topRow && row >= tablePtr->titleRows) {
	    if (full || (y+h < topY)) {
		return 0;
	    } else {
		h -= topY-y;
		y = topY;
	    }
	}
	/* re-set these according to changed coords */
	*rx = x; *ry = y; *rw = w; *rh = h;
    } else {
	/* If it is a hidden cell, then w,h is the row,col in user coords
	 * of the cell that spans over this one */
	return 0;
    }
    /* At this point, we know it is on the screen,
     * but not if we can see 100% of it (if we care)
     */
    if (full) {
	w0 = w; h0 = h;
    } else {
	/* if we don't care about seeing the whole thing, then
	 * make sure we at least see a pixel worth */
	w0 = h0 = 1;
    }
    /* Is the cell visible? */
    if (x<hl || y<hl || (x+w0)>(Tk_Width(tablePtr->tkwin)-hl)
	|| (y+h0)>(Tk_Height(tablePtr->tkwin)-hl)) {
	/* definitely off the screen */
	return 0;
    } else {
	/* if it was full, then w,h are already be properly constrained */
	if (!full) {
	    *rw = MIN(w, Tk_Width(tablePtr->tkwin)-hl-x);
	    *rh = MIN(h, Tk_Height(tablePtr->tkwin)-hl-y);
	}
	return 1;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TableWhatCell --
 *	Takes a x,y screen coordinate and determines what cell contains.
 *	that point.  This will return cells that are beyond the right/bottom
 *	edge of the viewable screen.
 *
 * Results:
 *	The row,col of the cell are placed in the pointers.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
void
TableWhatCell(register Table *tablePtr, int x, int y, int *row, int *col)
{
    int i;
    x = MAX(0, x); y = MAX(0, y);
    /* Adjust for table's global highlightthickness border */
    x -= tablePtr->highlightWidth;
    y -= tablePtr->highlightWidth;
    /* Adjust the x coord if not in the column titles to change display coords
     * into internal coords */
    x += (x < tablePtr->colStarts[tablePtr->titleCols]) ? 0 :
	tablePtr->colStarts[tablePtr->leftCol] -
	tablePtr->colStarts[tablePtr->titleCols];
    y += (y < tablePtr->rowStarts[tablePtr->titleRows]) ? 0 :
	tablePtr->rowStarts[tablePtr->topRow] -
	tablePtr->rowStarts[tablePtr->titleRows];
    x = MIN(x, tablePtr->maxWidth-1);
    y = MIN(y, tablePtr->maxHeight-1);
    for (i = 1; x >= tablePtr->colStarts[i]; i++);
    *col = i - 1;
    for (i = 1; y >= tablePtr->rowStarts[i]; i++);
    *row = i - 1;
    if (tablePtr->spanAffTbl && !(tablePtr->flags & AVOID_SPANS)) {
	char buf[INDEX_BUFSIZE];
	Tcl_HashEntry *entryPtr;

	/* We now correct the returned cell if this was "hidden" */
	TableMakeArrayIndex(*row+tablePtr->rowOffset,
			    *col+tablePtr->colOffset, buf);
	entryPtr = Tcl_FindHashEntry(tablePtr->spanAffTbl, buf);
	if ((entryPtr != NULL) &&
	    /* We have to make sure this was not already hidden
	     * that's an error */
	    ((char *)Tcl_GetHashValue(entryPtr) != NULL)) {
	    /* this is a "hidden" cell */
	    TableParseArrayIndex(row, col, (char *)Tcl_GetHashValue(entryPtr));
	    *row -= tablePtr->rowOffset;
	    *col -= tablePtr->colOffset;
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TableAtBorder --
 *	Takes a x,y screen coordinate and determines if that point is
 *	over a border.
 *
 * Results:
 *	The left/top row,col corresponding to that point are placed in
 *	the pointers.  The number of borders (+1 for row, +1 for col)
 *	hit is returned.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
int
TableAtBorder(Table * tablePtr, int x, int y, int *row, int *col)
{
    int i, brow, bcol, borders = 2, bd = tablePtr->defaultTag.bd;
    int dbd = 2*bd;
    x = MAX(0, x); y = MAX(0, y);
    x -= tablePtr->highlightWidth; y -= tablePtr->highlightWidth;
    /* Adjust the x coord if not in the column titles to change display
     * coords into internal coords */
    x += (x < tablePtr->colStarts[tablePtr->titleCols]) ? 0 :
	tablePtr->colStarts[tablePtr->leftCol] -
	tablePtr->colStarts[tablePtr->titleCols];
    x = MIN(x, tablePtr->maxWidth - 1);
    for (i = 1; i <= tablePtr->cols && x+dbd >= tablePtr->colStarts[i]; i++);
    if (x > tablePtr->colStarts[--i]+bd) {
	borders--;
	*col = -1;
	bcol = (i < tablePtr->leftCol && i >= tablePtr->titleCols) ?
	    tablePtr->titleCols-1 : i-1;
    } else {
	bcol = *col = (i < tablePtr->leftCol && i >= tablePtr->titleCols) ?
	    tablePtr->titleCols-1 : i-1;
    }
    y += (y < tablePtr->rowStarts[tablePtr->titleRows]) ? 0 :
	tablePtr->rowStarts[tablePtr->topRow] -
	tablePtr->rowStarts[tablePtr->titleRows];
    y = MIN(y, tablePtr->maxHeight - 1);
    for (i = 1; i <= tablePtr->rows && y+dbd >= tablePtr->rowStarts[i]; i++);
    if (y > tablePtr->rowStarts[--i]+bd) {
	borders--;
	*row = -1;
	brow = (i < tablePtr->topRow && i >= tablePtr->titleRows) ?
	    tablePtr->titleRows-1 : i-1;
    } else {
	brow = *row = (i < tablePtr->topRow && i >= tablePtr->titleRows) ?
	    tablePtr->titleRows-1 : i-1;
    }
    /* Have to account for spanning cells */
    if (tablePtr->spanAffTbl && !(tablePtr->flags & AVOID_SPANS) && borders) {
	char buf1[INDEX_BUFSIZE], buf2[INDEX_BUFSIZE];

	/* Have to account for "hidden" cells */
	if (*row != -1) {
	    TableMakeArrayIndex(brow+tablePtr->rowOffset,
				bcol+tablePtr->colOffset+1, buf1);
	    TableMakeArrayIndex(brow+tablePtr->rowOffset+1,
				bcol+tablePtr->colOffset+1, buf2);
	    if (Tcl_FindHashEntry(tablePtr->spanAffTbl, buf1) != NULL &&
		Tcl_FindHashEntry(tablePtr->spanAffTbl, buf2) != NULL) {
		borders--;
		*row = -1;
	    }
	}
	if (*col != -1) {
	    TableMakeArrayIndex(brow+tablePtr->rowOffset+1,
				bcol+tablePtr->colOffset, buf1);
	    TableMakeArrayIndex(brow+tablePtr->rowOffset+1,
				bcol+tablePtr->colOffset+1, buf2);
	    if (Tcl_FindHashEntry(tablePtr->spanAffTbl, buf1) != NULL &&
		Tcl_FindHashEntry(tablePtr->spanAffTbl, buf2) != NULL) {
		borders--;
		*col = -1;
	    }
	}
    }
    return borders;
}

/*
 *----------------------------------------------------------------------
 *
 * TableGetCellValue --
 *	Takes a row,col pair in user coords and returns the value for
 *	that cell.  This varies depending on what data source the
 *	user has selected.
 *
 * Results:
 *	The value of the cell is returned.  The return value is VOLATILE
 *	(do not free).
 *
 * Side effects:
 *	The value will be cached if caching is turned on.
 *
 *----------------------------------------------------------------------
 */
char *
TableGetCellValue(Table *tablePtr, int r, int c)
{
    register Tcl_Interp *interp = tablePtr->interp;
    char *result = NULL;
    char buf[INDEX_BUFSIZE];
    Tcl_HashEntry *entryPtr = NULL;
    int new = 1;
    TableMakeArrayIndex(r, c, buf);

    if (tablePtr->caching) {
	/* if we are caching, let's see if we have the value cached */
	entryPtr = Tcl_CreateHashEntry(tablePtr->cache, buf, &new);
	if (!new) {
	    result = (char *) Tcl_GetHashValue(entryPtr);
	    goto VALUE;
	}
    }
    if (tablePtr->command && tablePtr->useCmd) {

	if (LangDoCallback(interp, tablePtr->command, 1, 3, "%d %d %d",0,r,c) == TCL_ERROR) {
	    tablePtr->useCmd = 0;
	    tablePtr->dataSource &= ~DATA_COMMAND;
	    if (tablePtr->arrayVar)
		tablePtr->dataSource |= DATA_ARRAY;
	    Tcl_AddErrorInfo(interp, "\n\t(in -command evaled by table)");
	    Tcl_BackgroundError(interp);
	    TableInvalidateAll(tablePtr, 0);
	} else {
	    result = Tcl_GetResult(interp);
	}
	/* Tcl_FreeResult(interp); not support for perl/tk */
    } else if (tablePtr->arrayVar) {
	result = LangString(Tcl_GetVar2(interp, tablePtr->arrayVar, buf, TCL_GLOBAL_ONLY));
    }
    if (result == NULL)
	result = "";
    if (tablePtr->caching && entryPtr != NULL) {
	/* if we are caching, make sure we cache the returned value */
	/* entryPtr will have been set from above, but check to make sure
	 * someone didn't change caching during -command evaluation */
	char *val;
	val = (char *)ckalloc(strlen(result)+1);
	strcpy(val, result);
	Tcl_SetHashValue(entryPtr, val);
    }
VALUE:
#ifdef PROCS
    if (result != NULL) {
	/* Do we have procs, are we showing their value, is this a proc? */
	if (tablePtr->hasProcs && !tablePtr->showProcs && *result == '=' &&
	    !(r-tablePtr->rowOffset == tablePtr->activeRow &&
	      c-tablePtr->colOffset == tablePtr->activeCol)) {
	    Tcl_DString script;
	    /* provides a rough mutex on preventing proc loops */
	    entryPtr = Tcl_CreateHashEntry(tablePtr->inProc, buf, &new);
	    if (!new) {
		Tcl_SetHashValue(entryPtr, 1);
		Tcl_AddErrorInfo(interp, "\n\t(loop hit in proc evaled by table)");
		return result;
	    }
	    Tcl_SetHashValue(entryPtr, 0);
	    Tcl_DStringInit(&script);
	    ExpandPercents(tablePtr, result+1, r, c, result+1, (char *)NULL,
			   0, &script, 0);
	    if (Tcl_GlobalEval(interp, Tcl_DStringValue(&script)) != TCL_OK ||
		Tcl_GetHashValue(entryPtr) == 1) {
		Tcl_AddErrorInfo(interp, "\n\tin proc evaled by table:\n");
		Tcl_AddErrorInfo(interp, Tcl_DStringValue(&script));
		Tcl_BackgroundError(interp);
	    } else {
		result = Tcl_GetResult(interp);
	    }
	    /* Tcl_FreeResult(interp); Not Supported for perl/tk */
	    Tcl_DStringFree(&script);      
	    Tcl_DeleteHashEntry(entryPtr);
	}
    }
#endif
    return (result?result:"");
}

extern void	TableAddFlash _ANSI_ARGS_((Table *tablePtr, int row, int col));
/*
 *----------------------------------------------------------------------
 *
 * TableSetCellValue --
 *	Takes a row,col pair in user coords and saves the given value for
 *	that cell.  This varies depending on what data source the
 *	user has selected.
 *
 * Results:
 *	Returns TCL_ERROR or TCL_OK, depending on whether an error
 *	occured during set (ie: during evaluation of -command).
 *
 * Side effects:
 *	If the value is NULL (empty string), it will be unset from
 *	an array rather than set to the empty string.
 *
 *----------------------------------------------------------------------
 */
int
TableSetCellValue(Table *tablePtr, int r, int c, char *value)
{
    register Tcl_Interp *interp = tablePtr->interp;
    char buf[INDEX_BUFSIZE];
    int code = TCL_OK, flash = 0;

    TableMakeArrayIndex(r, c, buf);

    if (tablePtr->state == STATE_DISABLED) {
	return TCL_OK;
    }
    if (tablePtr->command && tablePtr->useCmd) {
	if (LangDoCallback(interp, tablePtr->command, 1, 4, "%d %d %d %s",1,r,c, value) == TCL_ERROR) {
	    /* An error resulted.  Prevent further triggering of the command
	     * and set up the error message. */
	    tablePtr->useCmd = 0;
	    tablePtr->dataSource &= ~DATA_COMMAND;
	    if (tablePtr->arrayVar)
		tablePtr->dataSource |= DATA_ARRAY;
	    Tcl_AddErrorInfo(interp, "\n\t(in command executed by table)");
	    Tcl_BackgroundError(interp);
	    code = TCL_ERROR;
	} else {
	    flash = 1;
	}
	Tcl_SetResult(interp, (char *) NULL, TCL_STATIC);
    } else if (tablePtr->arrayVar) {
	/* Warning: checking for \0 as the first char could invalidate
	 * allowing it as a valid first char */
	if ((value == NULL || *value == '\0') && tablePtr->sparse) {
	    /* perltk not supported  */
	    /* Tcl_UnsetVar2(interp, LangString(Tcl_GetVar(interp, tablePtr->arrayVar, TCL_GLOBAL_ONLY)), buf, TCL_GLOBAL_ONLY); */
	    /* Replaced with This (defined in tkTable.xs) */
	    tkTableUnsetElement(tablePtr->arrayVar, buf);
	} else if (Tcl_SetVar2(interp, tablePtr->arrayVar, buf, value,
			       TCL_GLOBAL_ONLY|TCL_LEAVE_ERR_MSG) == NULL) {
	    code = TCL_ERROR;
	}
    }
    if (code == TCL_ERROR) {
	return TCL_ERROR;
    }

    if (tablePtr->caching) {
	Tcl_HashEntry *entryPtr;
	int new;
	char *val;

	val = (char *)ckalloc(strlen(value)+1);
	strcpy(val, value);
	entryPtr = Tcl_CreateHashEntry(tablePtr->cache, buf, &new);
	Tcl_SetHashValue(entryPtr, val);
	flash = 1;
    }
    /* We do this conditionally because the var array already has
     * it's own check to flash */
    if (flash && tablePtr->flashMode) {
	r -= tablePtr->rowOffset;
	c -= tablePtr->colOffset;
	TableAddFlash(tablePtr, r, c);
	TableRefresh(tablePtr, r, c, CELL);
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TableSortCompareProc --
 *	This procedure is invoked by qsort to determine the proper
 *	ordering between two elements.
 *
 * Results:
 *	< 0 means first is "smaller" than "second", > 0 means "first"
 *	is larger than "second", and 0 means they should be treated
 *	as equal.
 *
 * Side effects:
 *	None, unless a user-defined comparison command does something
 *	weird.
 *
 *----------------------------------------------------------------------
 */
static int
TableSortCompareProc(first, second)
    CONST VOID *first, *second;		/* Elements to be compared. */
{
    int r1, c1, r2, c2;
    char *firstString;
    char *secondString; 
    firstString = LangString( *((Arg*) first));
    secondString = LangString(*((Arg*) second));


    /* This doesn't account for badly formed indices */
    sscanf(firstString, "%d,%d", &r1, &c1);
    sscanf(secondString, "%d,%d", &r2, &c2);
    if (r1 > r2) {
	return 1;
    } else if (r1 < r2) {
	return -1;
    } else if (c1 > c2) {
	return 1;
    } else if (c1 < c2) {
	return -1;
    }
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * TableCellSort --
 *	Sort a list of table cell elements (of form row,col)
 *
 * Results:
 *	Returns the sorted list of elements.  Because Tcl_Merge allocs
 *	the space for result, it must later be Tcl_Free'd by caller.
 *
 * Side effects:
 *	Behaviour undefined for ill-formed input list of elements.
 *
 *----------------------------------------------------------------------
 */
Arg
TableCellSort(Table *tablePtr, char *str)
{
    int listArgc;
    Arg *listArgv;
    Arg  result;
    Arg  argstr;
    argstr = LangStringArg(str);
    if (Tcl_ListObjGetElements(tablePtr->interp, argstr, &listArgc, &listArgv) != TCL_OK) {
        ckfree((char *) argstr);
	return LangStringArg(str);
    }
    qsort((VOID *) listArgv, (size_t) listArgc, sizeof (char *),
	  TableSortCompareProc);
    result = Tcl_NewListObj(listArgc, listArgv);
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * TableGetIcursor --
 *	Parses the argument as an index into the active cell string.
 *	Recognises 'end', 'insert' or an integer.  Constrains it to the
 *	size of the buffer.  This acts like a "SetIcursor" when *posn is NULL.
 *
 * Results:
 *	If (posn != NULL), then it gets the cursor position.
 *
 * Side effects:
 *	Can move cursor position.
 *
 *----------------------------------------------------------------------
 */
int
TableGetIcursor(Table *tablePtr, char *arg, int *posn)
{
    int tmp, len;

    len = strlen(tablePtr->activeBuf);
#ifdef TCL_UTF_MAX
    /* Need to base it off strlen to account for \x00 (Unicode null) */
    len = Tcl_NumUtfChars(tablePtr->activeBuf, len);
#endif
    /* ensure icursor didn't get out of sync */
    if (tablePtr->icursor > len) tablePtr->icursor = len;
    /* is this end */
    if (strcmp(arg, "end") == 0) {
	tmp = len;
    } else if (strcmp(arg, "insert") == 0) {
	tmp = tablePtr->icursor;
    } else {
	if (Tcl_GetInt(tablePtr->interp, LangStringArg(arg), &tmp) != TCL_OK) {
	    return TCL_ERROR;
	}
	tmp = MIN(MAX(0, tmp), len);
    }
    if (posn) {
	*posn = tmp;
    } else {
	tablePtr->icursor = tmp;
    }
    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 * TableGetIndex --
 *	Parse an index into a table and return either its value
 *	or an error.
 *
 * Results:
 *	A standard Tcl result.  If all went well, then *row,*col is
 *	filled in with the index corresponding to string.  If an
 *	error occurs then an error message is left in interp result.
 *	The index returned is in user coords.
 *
 * Side effects:
 *	Sets row,col index to an appropriately constrained user index.
 *
 *--------------------------------------------------------------
 */
int
TableGetIndex(tablePtr, str, row_p, col_p)
     register Table *tablePtr;	/* Table for which the index is being
				 * specified. */
     char *str;			/* Symbolic specification of cell in table. */
     int *row_p;		/* Where to store converted row. */
     int *col_p;		/* Where to store converted col. */
{
    int r, c, len = strlen(str);
    char dummy;

    /*
     * Note that all of these values will be adjusted by row/ColOffset
     */
    if (str[0] == '@') {	/* @x,y coordinate */ 
	int x, y;

	if (sscanf(str+1, "%d,%d%c", &x, &y, &dummy) != 2) {
	    /* Make sure it won't work for "2,3extrastuff" */
	    goto IndexError;
	}
	TableWhatCell(tablePtr, x, y, &r, &c);
	r += tablePtr->rowOffset;
	c += tablePtr->colOffset;
    } else if (*str == '-' || isdigit(str[0])) {
	if (sscanf(str, "%d,%d%c", &r, &c, &dummy) != 2) {
	    /* Make sure it won't work for "2,3extrastuff" */
	    goto IndexError;
	}
	/* ensure appropriate user index */
	r = MIN(MAX(tablePtr->rowOffset,r),
		tablePtr->rows-1+tablePtr->rowOffset);
	c = MIN(MAX(tablePtr->colOffset,c),
		tablePtr->cols-1+tablePtr->colOffset);
    } else if (len > 1 && strncmp(str, "active", len) == 0 ) {	/* active */
	if (tablePtr->flags & HAS_ACTIVE) {
	    r = tablePtr->activeRow+tablePtr->rowOffset;
	    c = tablePtr->activeCol+tablePtr->colOffset;
	} else {
	    Tcl_SetStringObj(Tcl_GetObjResult(tablePtr->interp),
			     "no \"active\" cell in table", -1);
	    return TCL_ERROR;
	}
    } else if (len > 1 && strncmp(str, "anchor", len) == 0) {	/* anchor */
	if (tablePtr->flags & HAS_ANCHOR) {
	    r = tablePtr->anchorRow+tablePtr->rowOffset;
	    c = tablePtr->anchorCol+tablePtr->colOffset;
	} else {
	    Tcl_SetStringObj(Tcl_GetObjResult(tablePtr->interp),
			     "no \"anchor\" cell in table", -1);
	    return TCL_ERROR;
	}
    } else if (strncmp(str, "end", len) == 0) {		/* end */
	r = tablePtr->rows-1+tablePtr->rowOffset;
	c = tablePtr->cols-1+tablePtr->colOffset;
    } else if (strncmp(str, "origin", len) == 0) {	/* origin */
	r = tablePtr->titleRows+tablePtr->rowOffset;
	c = tablePtr->titleCols+tablePtr->colOffset;
    } else if (strncmp(str, "topleft", len) == 0) {	/* topleft */
	r = tablePtr->topRow+tablePtr->rowOffset;
	c = tablePtr->leftCol+tablePtr->colOffset;
    } else if (strncmp(str, "bottomright", len) == 0) {	/* bottomright */
	TableGetLastCell(tablePtr, &r, &c);
	r += tablePtr->rowOffset;
	c += tablePtr->colOffset;
    } else {
    IndexError:
	Tcl_AppendStringsToObj(Tcl_GetObjResult(tablePtr->interp),
			       "bad table index \"", str, "\"", (char *)NULL);
	return TCL_ERROR;
    }

    /* Note: values are expected to be properly constrained 
     * as a user index by this point */
    if (row_p) *row_p = r;
    if (col_p) *col_p = c;
    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 * Table_SetCmd --
 *	This procedure is invoked to process the set method
 *	that corresponds to a widget managed by this module.
 *	See the user documentation for details on what it does.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *--------------------------------------------------------------
 */
int
Table_SetCmd(ClientData clientData, register Tcl_Interp *interp,
	     int objc, Tcl_Obj *CONST objv[])
{
    register Table *tablePtr = (Table *)clientData;
    int row, col, len, i, j, max;
    char *str;
    Tcl_Obj *resultPtr;

    /* sets any number of tags/indices to a given value */
    if (objc < 3) {
    CMD_SET_USAGE:
	Tcl_WrongNumArgs(interp, 2, objv,
			 "?row|col? index ?value? ?index value ...?");
	return TCL_ERROR;
    }

    /* make sure there is a data source to accept set */
    if (tablePtr->dataSource == DATA_NONE) {
	return TCL_OK;
    }

    resultPtr = Tcl_GetObjResult(interp);
    str = Tcl_GetStringFromObj(objv[2], &len);
    if (strncmp(str, "row", len) == 0 || strncmp(str, "col", len) == 0) {
	/* set row index list ?index list ...? */
	if (objc < 4) {
	    goto CMD_SET_USAGE;
	} else if (objc == 4) {
	    if (TableGetIndexObj(tablePtr, objv[3],
				 &row, &col) != TCL_OK) {
		return TCL_ERROR;
	    }
	    if (*str == 'r') {
		max = tablePtr->cols+tablePtr->colOffset;
		for (i=col; i<max; i++) {
		    str = TableGetCellValue(tablePtr, row, i);
		    Tcl_ListObjAppendElement(NULL, resultPtr,
					     Tcl_NewStringObj(str, -1));
		}
	    } else {
		max = tablePtr->rows+tablePtr->rowOffset;
		for (i=row; i<max; i++) {
		    str = TableGetCellValue(tablePtr, i, col);
		    Tcl_ListObjAppendElement(NULL, resultPtr,
					     Tcl_NewStringObj(str, -1));
		}
	    }
	} else if (tablePtr->state == STATE_NORMAL) {
	    int listc;
	    Tcl_Obj **listv;
	    /* make sure there are an even number of index/list pairs */
	    if (objc & 0) {
		goto CMD_SET_USAGE;
	    }
	    for (i = 3; i < objc-1; i += 2) {
		if ((TableGetIndexObj(tablePtr, objv[i],
				      &row, &col) != TCL_OK) ||
		    (Tcl_ListObjGetElements(interp, objv[i+1],
					    &listc, &listv) != TCL_OK)) {
		    return TCL_ERROR;
		}
		if (*str == 'r') {
		    max = col+MIN(tablePtr->cols+tablePtr->colOffset-col,
				  listc);
		    for (j = col; j < max; j++) {
			if (TableSetCellValue(tablePtr, row, j,
					      Tcl_GetString(listv[j-col]))
			    != TCL_OK) {
			    return TCL_ERROR;
			}
			if (row-tablePtr->rowOffset == tablePtr->activeRow &&
			    j-tablePtr->colOffset == tablePtr->activeCol) {
			    TableGetActiveBuf(tablePtr);
			}
			TableRefresh(tablePtr, row-tablePtr->rowOffset,
				     j-tablePtr->colOffset, CELL);
		    }
		} else {
		    max = row+MIN(tablePtr->rows+tablePtr->rowOffset-row,
				  listc);
		    for (j = row; j < max; j++) {
			if (TableSetCellValue(tablePtr, j, col,
					      Tcl_GetString(listv[j-row]))
			    != TCL_OK) {
			    return TCL_ERROR;
			}
			if (j-tablePtr->rowOffset == tablePtr->activeRow &&
			    col-tablePtr->colOffset == tablePtr->activeCol) {
			    TableGetActiveBuf(tablePtr);
			}
			TableRefresh(tablePtr, j-tablePtr->rowOffset,
				     col-tablePtr->colOffset, CELL);
		    }
		}
	    }
	}
    } else if (objc == 3) {
	/* set index */
	if (TableGetIndexObj(tablePtr, objv[2], &row, &col) != TCL_OK) {
	    return TCL_ERROR;
	} else {
	    Tcl_SetStringObj(resultPtr,
			     TableGetCellValue(tablePtr, row, col), -1);
	}
    } else {
	/* set index val ?index val ...? */
	/* make sure there are an even number of index/value pairs */
	if (objc & 1) {
	    goto CMD_SET_USAGE;
	}
	for (i = 2; i < objc-1; i += 2) {
	    if ((TableGetIndexObj(tablePtr, objv[i], &row, &col) != TCL_OK) ||
		(TableSetCellValue(tablePtr, row, col,
				   Tcl_GetString(objv[i+1])) != TCL_OK)) {
		return TCL_ERROR;
	    }
	    row -= tablePtr->rowOffset;
	    col -= tablePtr->colOffset;
	    if (row == tablePtr->activeRow && col == tablePtr->activeCol) {
		TableGetActiveBuf(tablePtr);
	    }
	    TableRefresh(tablePtr, row, col, CELL);
	}
    }
    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 * Table_SpanSet --
 *	Takes row,col in user coords and sets a span on the
 *	cell if possible
 *
 * Results:
 *	A standard Tcl result
 *
 * Side effects:
 *	The span can be constrained
 *
 *--------------------------------------------------------------
 */
static int
Table_SpanSet(register Table *tablePtr, int row, int col, int rs, int cs)
{
    Tcl_Interp *interp = tablePtr->interp;
    int i, j, new, ors, ocs, result = TCL_OK;
    Tcl_HashEntry *entryPtr;
    Tcl_HashSearch search;
    char *dbuf, buf[INDEX_BUFSIZE], cell[INDEX_BUFSIZE], span[INDEX_BUFSIZE];

    TableMakeArrayIndex(row, col, cell);

    if (tablePtr->spanTbl == NULL) {
	tablePtr->spanTbl = (Tcl_HashTable *) ckalloc(sizeof(Tcl_HashTable));
	Tcl_InitHashTable(tablePtr->spanTbl, TCL_STRING_KEYS);
	tablePtr->spanAffTbl = (Tcl_HashTable *) ckalloc(sizeof(Tcl_HashTable));
	Tcl_InitHashTable(tablePtr->spanAffTbl, TCL_STRING_KEYS);
    }

    /* first check in the affected cells table */
    if ((entryPtr=Tcl_FindHashEntry(tablePtr->spanAffTbl, cell)) != NULL) {
	/* We have to make sure this was not already hidden
	 * that's an error */
	if ((char *)Tcl_GetHashValue(entryPtr) != NULL) {
	    Tcl_AppendStringsToObj(Tcl_GetObjResult(interp),
				   "cannot set spanning on hidden cell ",
				   cell, (char *) NULL);
	    return TCL_ERROR;
	}
    }
    /* do constraints on the spans
     * title cells must not expand beyond the titles
     * other cells can't expand negatively into title area
     */
    if ((row-tablePtr->rowOffset < tablePtr->titleRows) &&
	(row-tablePtr->rowOffset+rs >= tablePtr->titleRows)) {
	rs = tablePtr->titleRows-(row-tablePtr->rowOffset)-1;
    }
    if ((col-tablePtr->colOffset < tablePtr->titleCols) &&
	(col-tablePtr->colOffset+cs >= tablePtr->titleCols)) {
	cs = tablePtr->titleCols-(col-tablePtr->colOffset)-1;
    }
    rs = MAX(0, rs);
    cs = MAX(0, cs);

    /* then work in the span cells table */
    if ((entryPtr = Tcl_FindHashEntry(tablePtr->spanTbl, cell)) != NULL) {
	/* We have to readjust for what was there first */
	TableParseArrayIndex(&ors, &ocs, (char *)Tcl_GetHashValue(entryPtr));
	ckfree((char *) Tcl_GetHashValue(entryPtr));
	Tcl_DeleteHashEntry(entryPtr);
	for (i = row; i <= row+ors; i++) {
	    for (j = col; j <= col+ocs; j++) {
		TableMakeArrayIndex(i, j, buf);
		entryPtr = Tcl_FindHashEntry(tablePtr->spanAffTbl, buf);
		if (entryPtr != NULL) {
		    Tcl_DeleteHashEntry(entryPtr);
		}
		TableRefresh(tablePtr, i-tablePtr->rowOffset,
			     j-tablePtr->colOffset, CELL);
	    }
	}
    } else {
	ors = ocs = 0;
    }

    /* calc to make sure that span is OK */
    for (i = row; i <= row+rs; i++) {
	for (j = col; j <= col+cs; j++) {
	    TableMakeArrayIndex(i, j, buf);
	    entryPtr = Tcl_FindHashEntry(tablePtr->spanAffTbl, buf);
	    if (entryPtr != NULL) {
		/* Something already spans here */
		Tcl_AppendStringsToObj(Tcl_GetObjResult(interp),
				       "cannot overlap already spanning cell ",
				       buf, (char *) NULL);
		result = TCL_ERROR;
		rs = ors;
		cs = ocs;
		break;
	    }
	}
	if (result == TCL_ERROR)
	    break;
    }

    /* 0,0 span means set to unspanned again */
    if (rs == 0 && cs == 0) {
	entryPtr = Tcl_FindHashEntry(tablePtr->spanTbl, cell);
	if (entryPtr != NULL) {
	    ckfree((char *) Tcl_GetHashValue(entryPtr));
	    Tcl_DeleteHashEntry(entryPtr);
	}
	entryPtr = Tcl_FindHashEntry(tablePtr->spanAffTbl, cell);
	if (entryPtr != NULL) {
	    Tcl_DeleteHashEntry(entryPtr);
	}
	if (Tcl_FirstHashEntry(tablePtr->spanTbl, &search) == NULL) {
	    /* There are no more spans, so delete tables to improve
	     * performance of TableCellCoords */
	    Tcl_DeleteHashTable(tablePtr->spanTbl);
	    ckfree((char *) (tablePtr->spanTbl));
	    Tcl_DeleteHashTable(tablePtr->spanAffTbl);
	    ckfree((char *) (tablePtr->spanAffTbl));
	    tablePtr->spanTbl = NULL;
	    tablePtr->spanAffTbl = NULL;
	}
	return result;
    }

    /* Make sure there is no extra stuff */
    TableMakeArrayIndex(rs, cs, span);

    /* Set affected cell table to a NULL value */
    entryPtr = Tcl_CreateHashEntry(tablePtr->spanAffTbl, cell, &new);
    Tcl_SetHashValue(entryPtr, (char *) NULL);
    /* set the spanning cells table with span value */
    entryPtr = Tcl_CreateHashEntry(tablePtr->spanTbl, cell, &new);
    dbuf = (char *)ckalloc(strlen(span)+1);
    strcpy(dbuf, span);
    Tcl_SetHashValue(entryPtr, dbuf);
    dbuf = Tcl_GetHashKey(tablePtr->spanTbl, entryPtr);
    /* Set other affected cells */
    EmbWinUnmap(tablePtr, row-tablePtr->rowOffset, row+rs-tablePtr->rowOffset,
		col-tablePtr->colOffset, col+cs-tablePtr->colOffset);
    for (i = row; i <= row+rs; i++) {
	for (j = col; j <= col+cs; j++) {
	    TableMakeArrayIndex(i, j, buf);
	    entryPtr = Tcl_CreateHashEntry(tablePtr->spanAffTbl, buf, &new);
	    if (!(i == row && j == col)) {
		Tcl_SetHashValue(entryPtr, (char *) dbuf);
	    }
	}
    }
    TableRefresh(tablePtr, row-tablePtr->rowOffset,
		 col-tablePtr->colOffset, CELL);
    return result;
}

/*
 *--------------------------------------------------------------
 *
 * Table_SpanCmd --
 *	This procedure is invoked to process the span method
 *	that corresponds to a widget managed by this module.
 *	See the user documentation for details on what it does.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *--------------------------------------------------------------
 */
int
Table_SpanCmd(ClientData clientData, register Tcl_Interp *interp,
	      int objc, Tcl_Obj *CONST objv[])
{
    register Table *tablePtr = (Table *) clientData;
    int rs, cs, row, col, i;
    Tcl_HashEntry *entryPtr;
    Tcl_Obj *objPtr, *resultPtr;

    if (objc < 2 || (objc > 4 && (objc&1))) {
	Tcl_WrongNumArgs(interp, 2, objv,
			 "?index? ?rows,cols index rows,cols ...?");
	return TCL_ERROR;
    }

    resultPtr = Tcl_GetObjResult(interp);

    if (objc == 2) {
	if (tablePtr->spanTbl) {
	    Tcl_HashSearch search;

	    for (entryPtr = Tcl_FirstHashEntry(tablePtr->spanTbl, &search);
		 entryPtr != NULL; entryPtr = Tcl_NextHashEntry(&search)) {
		objPtr = Tcl_NewStringObj(Tcl_GetHashKey(tablePtr->spanTbl,
							 entryPtr), -1);
		Tcl_ListObjAppendElement(NULL, resultPtr, objPtr);
		objPtr = Tcl_NewStringObj((char *) Tcl_GetHashValue(entryPtr),
					  -1);
		Tcl_ListObjAppendElement(NULL, resultPtr, objPtr);
	    }
	}
	return TCL_OK;
    } else if (objc == 3) {
	if (TableGetIndexObj(tablePtr, objv[2], &row, &col) == TCL_ERROR) {
	    return TCL_ERROR;
	}
	/* Just return the spanning values of the one cell */
	if (tablePtr->spanTbl &&
	    (entryPtr = Tcl_FindHashEntry(tablePtr->spanTbl,
					  Tcl_GetString(objv[2]))) != NULL) {
	    Tcl_SetStringObj(resultPtr,
			     (char *)Tcl_GetHashValue(entryPtr), -1);
	}
	return TCL_OK;
    } else {
	for (i = 2; i < objc-1; i += 2) {
	    if (TableGetIndexObj(tablePtr, objv[i], &row, &col) == TCL_ERROR ||
		(TableParseArrayIndex(&rs, &cs,
				      Tcl_GetString(objv[i+1])) != 2) ||
		Table_SpanSet(tablePtr, row, col, rs, cs) == TCL_ERROR) {
		return TCL_ERROR;
	    }
	}
    }
    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 * Table_HiddenCmd --
 *	This procedure is invoked to process the hidden method
 *	that corresponds to a widget managed by this module.
 *	See the user documentation for details on what it does.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *--------------------------------------------------------------
 */
int
Table_HiddenCmd(ClientData clientData, register Tcl_Interp *interp,
		int objc, Tcl_Obj *CONST objv[])
{
    register Table *tablePtr = (Table *) clientData;
    int i, row, col;
    Tcl_HashEntry *entryPtr;
    char *span;

    if (objc < 2) {
	Tcl_WrongNumArgs(interp, 2, objv, "?index? ?index ...?");
	return TCL_ERROR;
    }
    if (tablePtr->spanTbl == NULL) {
	/* Avoid the whole thing if we have no spans */
	if (objc > 3) {
	    Tcl_SetBooleanObj(Tcl_GetObjResult(interp), 0);
	}
	return TCL_OK;
    }
    if (objc == 2) {
	/* return all "hidden" cells */
	Tcl_DString cells;
	Tcl_HashSearch search;

	Tcl_DStringInit(&cells);
	for (entryPtr = Tcl_FirstHashEntry(tablePtr->spanAffTbl, &search);
	     entryPtr != NULL; entryPtr = Tcl_NextHashEntry(&search)) {
	    if ((span = (char *) Tcl_GetHashValue(entryPtr)) == NULL) {
		/* this is actually a spanning cell */
		continue;
	    }
	    Tcl_DStringAppendElement(&cells,
				     Tcl_GetHashKey(tablePtr->spanAffTbl,
						    entryPtr));
	}
	span = LangString(TableCellSort(tablePtr, Tcl_DStringValue(&cells)));
	if (span != NULL) {
	    Tcl_SetResult(interp, span, TCL_DYNAMIC);
	}
	Tcl_DStringFree(&cells);
	return TCL_OK;
    }
    if (objc == 3) {
	if (TableGetIndexObj(tablePtr, objv[2], &row, &col) != TCL_OK) {
	    return TCL_ERROR;
	}
	/* Just return the spanning values of the one cell */
	entryPtr = Tcl_FindHashEntry(tablePtr->spanAffTbl,
				     Tcl_GetString(objv[2]));
	if (entryPtr != NULL &&
	    (span = (char *)Tcl_GetHashValue(entryPtr)) != NULL) {
	    /* this is a hidden cell */
	    Tcl_SetStringObj(Tcl_GetObjResult(interp), span, -1);
	}
	return TCL_OK;
    }
    for (i = 2; i < objc; i++) {
	if (TableGetIndexObj(tablePtr, objv[i], &row, &col) == TCL_ERROR) {
	    return TCL_ERROR;
	}
	entryPtr = Tcl_FindHashEntry(tablePtr->spanAffTbl,
				     Tcl_GetString(objv[i]));
	if (entryPtr != NULL &&
	    (char *)Tcl_GetHashValue(entryPtr) != NULL) {
	    /* this is a hidden cell */
	    continue;
	}
	/* We only reach here if it doesn't satisfy "hidden" criteria */
	Tcl_SetBooleanObj(Tcl_GetObjResult(interp), 0);
	return TCL_OK;
    }
    Tcl_SetBooleanObj(Tcl_GetObjResult(interp), 1);
    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 * TableSpanSanCheck --
 *	This procedure is invoked by TableConfigure to make sure
 *	that spans are kept sane according to the docs.
 *	See the user documentation for details on what it does.
 *
 * Results:
 *	void.
 *
 * Side effects:
 *	Spans in title areas can be reconstrained.
 *
 *--------------------------------------------------------------
 */
void
TableSpanSanCheck(register Table *tablePtr)
{
    int rs, cs, row, col, reset;
    Tcl_HashEntry *entryPtr;
    Tcl_HashSearch search;

    if (tablePtr->spanTbl == NULL) {
	return;
    }

    for (entryPtr = Tcl_FirstHashEntry(tablePtr->spanTbl, &search);
	 entryPtr != NULL; entryPtr = Tcl_NextHashEntry(&search)) {
	reset = 0;
	TableParseArrayIndex(&row, &col,
			     Tcl_GetHashKey(tablePtr->spanTbl, entryPtr));
	TableParseArrayIndex(&rs, &cs,
			     (char *) Tcl_GetHashValue(entryPtr));
	if ((row-tablePtr->rowOffset < tablePtr->titleRows) &&
	    (row-tablePtr->rowOffset+rs >= tablePtr->titleRows)) {
	    rs = tablePtr->titleRows-(row-tablePtr->rowOffset)-1;
	    reset = 1;
	}
	if ((col-tablePtr->colOffset < tablePtr->titleCols) &&
	    (col-tablePtr->colOffset+cs >= tablePtr->titleCols)) {
	    cs = tablePtr->titleCols-(col-tablePtr->colOffset)-1;
	    reset = 1;
	}
	if (reset) {
	    Table_SpanSet(tablePtr, row, col, rs, cs);
	}
    }
}