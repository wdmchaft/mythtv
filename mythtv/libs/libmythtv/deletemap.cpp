#include <cmath>
#include <stdint.h>

#include "mythlogging.h"
#include "mythcontext.h"
#include "osd.h"
#include "deletemap.h"
#include "mythplayer.h"

#define LOC     QString("DelMap: ")
#define EDIT_CHECK do { \
    if(!m_editing) { \
        LOG(VB_GENERAL, LOG_ERR, LOC + "Cannot edit outside edit mode."); \
        return; \
    } \
} while(0)

DeleteMapUndoEntry::DeleteMapUndoEntry(frm_dir_map_t dm, QString msg) :
    deleteMap(dm), message(msg) { }

DeleteMapUndoEntry::DeleteMapUndoEntry(void)
{
    frm_dir_map_t dm;
    deleteMap = dm;
    message = "";
}

void DeleteMap::Push(QString undoMessage)
{
    DeleteMapUndoEntry entry(m_deleteMap, undoMessage);
    // Remove all "redo" entries
    while (m_undoStack.size() > m_undoStackPointer + 1)
        m_undoStack.pop_back();
    m_undoStack.append(entry);
    m_undoStackPointer ++;
    SaveMap(0, true);
}

bool DeleteMap::Undo(void)
{
    if (!HasUndo())
        return false;
    m_undoStackPointer --;
    m_deleteMap = m_undoStack[m_undoStackPointer].deleteMap;
    m_changed = true;
    SaveMap(0, true);
    return true;
}

bool DeleteMap::Redo(void)
{
    if (!HasRedo())
        return false;
    m_undoStackPointer ++;
    m_deleteMap = m_undoStack[m_undoStackPointer].deleteMap;
    m_changed = true;
    SaveMap(0, true);
    return true;
}

QString DeleteMap::GetUndoMessage(void) const
{
    return (HasUndo() ? m_undoStack[m_undoStackPointer].message :
            QObject::tr("(Nothing to undo)"));
}

QString DeleteMap::GetRedoMessage(void) const
{
    return (HasRedo() ? m_undoStack[m_undoStackPointer + 1].message :
            QObject::tr("(Nothing to redo)"));
}

bool DeleteMap::HandleAction(QString &action, uint64_t frame,
                             uint64_t played, uint64_t total, double rate)
{
    bool handled = true;
    if (action == ACTION_UP)
        UpdateSeekAmount(1, rate);
    else if (action == ACTION_DOWN)
        UpdateSeekAmount(-1, rate);
    else if (action == ACTION_CLEARMAP)
        Clear(QObject::tr("Clear Cuts"));
    else if (action == ACTION_INVERTMAP)
        ReverseAll(total);
    else if (action == "MOVEPREV")
        MoveRelative(frame, total, false);
    else if (action == "MOVENEXT")
        MoveRelative(frame, total, true);
    else if (action == "CUTTOBEGINNING")
        Add(frame, total, MARK_CUT_END, QObject::tr("Cut to Beginning"));
    else if (action == "CUTTOEND")
    {
        Add(frame, total, MARK_CUT_START, QObject::tr("Cut to End"));
        // If the recording is still in progress, add an explicit end
        // mark at the end.
        if (m_ctx->player && m_ctx->player->IsWatchingInprogress())
            Add(total - 1, total, MARK_CUT_END, "");
    }
    else if (action == "NEWCUT")
        NewCut(frame, total);
    else if (action == "DELETE")
        Delete(frame, total, QObject::tr("Delete"));
    else if (action == "UNDO")
        Undo();
    else if (action == "REDO")
        Redo();
    else
        handled = false;
    return handled;
}

void DeleteMap::UpdateSeekAmount(int change, double framerate)
{
    m_seekamountpos += change;
    if (m_seekamountpos > 9)
        m_seekamountpos = 9;
    if (m_seekamountpos < 0)
        m_seekamountpos = 0;

    m_seekText = "";
    switch (m_seekamountpos)
    {
        case 0: m_seekText = QObject::tr("cut point"); m_seekamount = -2; break;
        case 1: m_seekText = QObject::tr("keyframe"); m_seekamount = -1; break;
        case 2: m_seekText = QObject::tr("1 frame"); m_seekamount = 1; break;
        case 3: m_seekText = QObject::tr("0.5 seconds"); m_seekamount = (int)roundf(framerate / 2); break;
        case 4: m_seekText = QObject::tr("%n second(s)", "", 1); m_seekamount = (int)roundf(framerate); break;
        case 5: m_seekText = QObject::tr("%n second(s)", "", 5); m_seekamount = (int)roundf(framerate * 5); break;
        case 6: m_seekText = QObject::tr("%n second(s)", "", 20); m_seekamount = (int)roundf(framerate * 20); break;
        case 7: m_seekText = QObject::tr("%n minute(s)", "", 1); m_seekamount = (int)roundf(framerate * 60); break;
        case 8: m_seekText = QObject::tr("%n minute(s)", "", 5); m_seekamount = (int)roundf(framerate * 300); break;
        case 9: m_seekText = QObject::tr("%n minute(s)", "", 10); m_seekamount = (int)roundf(framerate * 600); break;
        default: m_seekText = QObject::tr("error"); m_seekamount = (int)roundf(framerate); break;
    }
}

static QString createTimeString(uint64_t frame, uint64_t total,
                                double frame_rate, bool full_resolution)
{
    int secs   = (int)(frame / frame_rate);
    int frames = frame - (int)(secs * frame_rate);
    int totalSecs = (int)(total / frame_rate);
    QString timestr;
    if (totalSecs >= 3600)
        timestr = QString::number(secs / 3600) + ":";
    timestr += QString("%1").arg((secs / 60) % 60, 2, 10, QChar(48)) +
        QString(":%1").arg(secs % 60, 2, 10, QChar(48));
    if (full_resolution)
        timestr += QString(".%1").arg(frames, 2, 10, QChar(48));
    return timestr;
}

 /**
 * \brief Show and update the edit mode On Screen Display. The cut regions
 *        are only refreshed if the deleteMap has been updated.
 */
void DeleteMap::UpdateOSD(uint64_t frame, uint64_t total, double frame_rate,
                          OSD *osd)
{
    if (!osd || !m_ctx)
        return;
    CleanMap(total);

    InfoMap infoMap;
    m_ctx->LockPlayingInfo(__FILE__, __LINE__);
    if (m_ctx->playingInfo)
        m_ctx->playingInfo->ToMap(infoMap);
    infoMap.detach();
    m_ctx->UnlockPlayingInfo(__FILE__, __LINE__);

    QString cutmarker = " ";
    if (IsInDelete(frame))
        cutmarker = QObject::tr("cut");

    QString timestr = createTimeString(frame, total, frame_rate, true);
    uint64_t relTotal = TranslatePositionAbsToRel(total);
    QString relTimeDisplay = createTimeString(TranslatePositionAbsToRel(frame),
                                              relTotal, frame_rate, false);
    QString relLengthDisplay = createTimeString(relTotal,
                                                relTotal, frame_rate, false);
    infoMap["timedisplay"]  = timestr;
    infoMap["framedisplay"] = QString::number(frame);
    infoMap["cutindicator"] = cutmarker;
    infoMap["title"]        = QObject::tr("Edit");
    infoMap["seekamount"]   = m_seekText;;
    infoMap["reltimedisplay"] = relTimeDisplay;
    infoMap["rellengthdisplay"] = relLengthDisplay;
    infoMap["fulltimedisplay"] = timestr + " (" +
        QObject::tr("%1 of %2").arg(relTimeDisplay).arg(relLengthDisplay) + ")";

    QHash<QString,float> posMap;
    posMap.insert("position", (float)((double)frame/(double)total));
    osd->SetValues("osd_program_editor", posMap, kOSDTimeout_None);
    osd->SetText("osd_program_editor", infoMap,  kOSDTimeout_None);
    if (m_changed || total != m_cachedTotalForOSD)
        osd->SetRegions("osd_program_editor", m_deleteMap, total);
    m_changed = false;
    m_cachedTotalForOSD = total;
}

/// Set the edit mode and optionally hide the edit mode OSD.
void DeleteMap::SetEditing(bool edit, OSD *osd)
{
    if (osd && !edit)
        osd->HideWindow("osd_program_editor");
    m_editing = edit;
}

/// Update the editing status in the file's ProgramInfo.
void DeleteMap::SetFileEditing(bool edit)
{
    if (m_ctx)
    {
        m_ctx->LockPlayingInfo(__FILE__, __LINE__);
        if (m_ctx->playingInfo)
            m_ctx->playingInfo->SetEditing(edit);
        m_ctx->UnlockPlayingInfo(__FILE__, __LINE__);
    }
}

/// Determines whether the file is currently in edit mode.
bool DeleteMap::IsFileEditing(void)
{
    bool result = false;
    if (m_ctx)
    {
        m_ctx->LockPlayingInfo(__FILE__, __LINE__);
        if (m_ctx->playingInfo)
            result = m_ctx->playingInfo->QueryIsEditing();
        m_ctx->UnlockPlayingInfo(__FILE__, __LINE__);
    }
    return result;
}

bool DeleteMap::IsEmpty(void) const
{
    return m_deleteMap.empty();
}

/// Clears the deleteMap.
void DeleteMap::Clear(QString undoMessage)
{
    m_deleteMap.clear();
    m_changed = true;
    if (!undoMessage.isEmpty())
        Push(undoMessage);
}

/// Reverses the direction of each mark in the map.
void DeleteMap::ReverseAll(uint64_t total)
{
    EDIT_CHECK;
    frm_dir_map_t::Iterator it = m_deleteMap.begin();
    for ( ; it != m_deleteMap.end(); ++it)
        Add(it.key(), it.value() == MARK_CUT_END ? MARK_CUT_START :
                                                   MARK_CUT_END);
    CleanMap(total);
    Push(QObject::tr("Reverse Cuts"));
}

/**
 * \brief Add a new mark of the given type. Before the new mark is added, any
 *        existing redundant mark of that type is removed. This simplifies
 *        the cleanup code.
 */
void DeleteMap::Add(uint64_t frame, uint64_t total, MarkTypes type,
                    QString undoMessage)
{
    EDIT_CHECK;
    if ((MARK_CUT_START != type) && (MARK_CUT_END != type) &&
        (MARK_PLACEHOLDER != type))
        return;

    frm_dir_map_t::Iterator find_temporary = m_deleteMap.find(frame);
    if (find_temporary != m_deleteMap.end())
    {
        if (MARK_PLACEHOLDER == find_temporary.value())
        {
            // Delete the temporary mark before putting a real mark at its
            // location
            Delete(frame, total);
        }
        else // Don't add a mark on top of a mark
            return;
    }

    int       lasttype  = MARK_UNSET;
    long long lastframe = -1;
    long long remove    = -1;
    QMutableMapIterator<uint64_t, MarkTypes> it(m_deleteMap);

    if (type == MARK_CUT_END)
    {
        // remove curent end marker if it exists
        while (it.hasNext())
        {
            it.next();
            if (it.key() > frame)
            {
                if ((lasttype == MARK_CUT_END) && (lastframe > -1))
                    remove = lastframe;
                break;
            }
            lasttype  = it.value();
            lastframe = it.key();
        }
        if ((remove < 0) && (lasttype == MARK_CUT_END) &&
            (lastframe > -1) && (lastframe < (int64_t)frame))
            remove = lastframe;
    }
    else if (type == MARK_CUT_START)
    {
        // remove curent start marker if it exists
        it.toBack();
        while (it.hasPrevious())
        {
            it.previous();
            if (it.key() <= frame)
            {
                if (lasttype == MARK_CUT_START && (lastframe > -1))
                    remove = lastframe;
                break;
            }
            lasttype  = it.value();
            lastframe = it.key();
        }
        if ((remove < 0) && (lasttype == MARK_CUT_START) &&
            (lastframe > -1) && (lastframe > (int64_t)frame))
            remove = lastframe;
    }

    if (remove > -1)
        Delete((uint64_t)remove);
    Add(frame, type);
    CleanMap(total);
    if (!undoMessage.isEmpty())
        Push(undoMessage);
}

/// Remove the mark at the given frame.
void DeleteMap::Delete(uint64_t frame, uint64_t total, QString undoMessage)
{
    EDIT_CHECK;
    if (m_deleteMap.isEmpty())
        return;

    uint64_t prev = GetNearestMark(frame, total, false);
    uint64_t next = GetNearestMark(frame, total, true);

    // If frame is a cut point, GetNearestMark() would return the previous/next
    // mark (not this frame), so check to see if we need to use frame, instead
    frm_dir_map_t::Iterator it = m_deleteMap.find(frame);
    if (it != m_deleteMap.end())
    {
        int type = it.value();
        if (MARK_PLACEHOLDER == type)
            next = prev = frame;
        else if (MARK_CUT_END == type)
            next  = frame;
        else if (MARK_CUT_START == type)
            prev = frame;
    }

    Delete(prev);
    if (prev != next)
        Delete(next);
    CleanMap(total);
    if (!undoMessage.isEmpty())
        Push(undoMessage);
}

/// Add a new cut marker (to start or end a cut region)
void DeleteMap::NewCut(uint64_t frame, uint64_t total)
{
    EDIT_CHECK;

    // find any existing temporary marker to determine cut range
    int64_t existing = -1;
    frm_dir_map_t::Iterator it;
    for (it = m_deleteMap.begin() ; it != m_deleteMap.end(); ++it)
    {
        if (MARK_PLACEHOLDER == it.value())
        {
            existing = it.key();
            break;
        }
    }

    if (existing > -1)
    {
        uint64_t otherframe = static_cast<uint64_t>(existing);
        if (otherframe == frame)
            Delete(otherframe);
        else
        {
            uint64_t startframe;
            uint64_t endframe;
            int64_t cut_start = -1;
            int64_t cut_end = -1;
            if (IsInDelete(frame))
            {
                MarkTypes type = MARK_UNSET;
                cut_start = GetNearestMark(frame, total, false);
                cut_end = GetNearestMark(frame, total, true);
                frm_dir_map_t::Iterator it = m_deleteMap.find(frame);
                if (it != m_deleteMap.end())
                    type = it.value();
                if (MARK_CUT_START == type)
                {
                    cut_start = frame;
                }
                else if (MARK_CUT_END == type)
                {
                    cut_end = frame;
                }
            }

            if (otherframe < frame)
            {
                startframe = otherframe;
                endframe = cut_end != -1 ? static_cast<uint64_t>(cut_end)
                                         : frame;
            }
            else
            {
                startframe = cut_start != -1 ? static_cast<uint64_t>(cut_start)
                                             : frame;
                endframe = otherframe;
            }

            // Don't place a cut marker on first or last frame; instead cut
            // to beginning or end
            if (startframe == 1)
                startframe = 0;
            if (endframe >= total - 1)
                endframe = total;

            // Don't cut the entire recording
            if ((startframe == 0) && (endframe == total))
            {
                LOG(VB_GENERAL, LOG_CRIT, LOC +
                    "Refusing to cut entire recording.");
                return;
            }

            Delete(otherframe);
            Add(startframe, MARK_CUT_START);
            Add(endframe, MARK_CUT_END);
            // Clear out any markers between the start and end frames
            otherframe = 0;
            frm_dir_map_t::Iterator it = m_deleteMap.find(startframe);
            for ( ; it != m_deleteMap.end() && otherframe < endframe; ++it)
            {
                otherframe = it.key();
                if ((startframe < otherframe) && (endframe > otherframe))
                {
                    LOG(VB_PLAYBACK, LOG_INFO, LOC +
                        QString("Deleting bounded marker: %1").arg(otherframe));
                    Delete(otherframe);
                }
            }
        }
    }
    else
        Add(frame, MARK_PLACEHOLDER);

    CleanMap(total);
    Push(QObject::tr("New Cut"));
}

/// Move the previous (!right) or next (right) cut to frame.
void DeleteMap::MoveRelative(uint64_t frame, uint64_t total, bool right)
{
    frm_dir_map_t::Iterator it = m_deleteMap.find(frame);
    if (it != m_deleteMap.end())
    {
        int type = it.value();
        if (((MARK_CUT_START == type) && !right) ||
            ((MARK_CUT_END == type) && right))
        {
            // If on a mark, don't move a mark from a different cut region;
            // instead, "move" this mark onto itself
            return;
        }
        else if (((MARK_CUT_START == type) && right) ||
                 ((MARK_CUT_END == type) && !right))
        {
            // If on a mark, don't collapse a cut region to 0;
            // instead, delete the region
            Delete(frame, total, QObject::tr("Delete"));
            return;
        }
        else if (MARK_PLACEHOLDER == type)
        {
            // Delete the temporary mark before putting a real mark at its
            // location
            Delete(frame, total);
        }
    }

    uint64_t from = GetNearestMark(frame, total, right);
    Move(from, frame, total);
}

/// Move an existing mark to a new frame.
void DeleteMap::Move(uint64_t frame, uint64_t to, uint64_t total)
{
    EDIT_CHECK;
    MarkTypes type = Delete(frame);
    if (MARK_UNSET == type)
    {
        if (frame == 0)
            type = MARK_CUT_START;
        else if (frame == total)
            type = MARK_CUT_END;
    }
    Add(to, total, type, QObject::tr("Move Mark"));
}

/// Private addition to the deleteMap.
void DeleteMap::Add(uint64_t frame, MarkTypes type)
{
    m_deleteMap[frame] = type;
    m_changed = true;
}

/// Private removal from the deleteMap.
MarkTypes DeleteMap::Delete(uint64_t frame)
{
    if (m_deleteMap.contains(frame))
    {
        m_changed = true;
        return m_deleteMap.take(frame);
    }
    return MARK_UNSET;
}

/**
 * \brief Returns true if the given frame is deemed to be within a region
 *        that should be cut.
 */
bool DeleteMap::IsInDelete(uint64_t frame) const
{
    if (m_deleteMap.isEmpty())
        return false;

    frm_dir_map_t::const_iterator it = m_deleteMap.find(frame);
    if (it != m_deleteMap.end())
        return true;

    int      lasttype  = MARK_UNSET;
    uint64_t lastframe = -1;
    for (it = m_deleteMap.begin() ; it != m_deleteMap.end(); ++it)
    {
        if (it.key() > frame)
            return MARK_CUT_END == it.value();
        lasttype  = it.value();
        lastframe = it.key();
    }

    if (lasttype == MARK_CUT_START && lastframe <= frame)
        return true;
    return false;
}

/**
 * \brief Returns true if the given frame is a temporary/placeholder mark
 */
bool DeleteMap::IsTemporaryMark(uint64_t frame) const
{
    if (m_deleteMap.isEmpty())
        return false;

    frm_dir_map_t::const_iterator it = m_deleteMap.find(frame);
    return (it != m_deleteMap.end()) && (MARK_PLACEHOLDER == it.value());
}

/**
 * \brief Returns the next or previous mark. If these do not exist,
 *        returns either zero (the first frame) or total (the last
 *        frame).  If hasMark is non-NULL, it is set to true if the
 *        next/previous mark exists, and false otherwise.
 */
uint64_t DeleteMap::GetNearestMark(
    uint64_t frame, uint64_t total, bool right,
    bool *hasMark) const
{
    uint64_t result;
    if (hasMark)
        *hasMark = true;
    frm_dir_map_t::const_iterator it = m_deleteMap.begin();
    if (right)
    {
        result = total;
        for (; it != m_deleteMap.end(); ++it)
            if (it.key() > frame)
                return it.key();
        if (hasMark)
            *hasMark = false;
    }
    else
    {
        result = 0;
        for (; it != m_deleteMap.end(); ++it)
        {
            if (it.key() >= frame)
                return result;
            result = it.key();
        }
    }
    return result;
}

/**
 * \brief Returns true if a temporary placeholder mark is defined.
 */
bool DeleteMap::HasTemporaryMark(void) const
{
    if (!m_deleteMap.isEmpty())
    {
        frm_dir_map_t::const_iterator it = m_deleteMap.begin();
        for ( ; it != m_deleteMap.end(); ++it)
            if (MARK_PLACEHOLDER == it.value())
                return true;
    }

    return false;
}

/**
 * \brief Removes redundant marks and ensures the markup sequence is valid.
 *        A valid sequence is 1 or more marks of alternating values and does
 *        not include the first or last frames.
 */
void DeleteMap::CleanMap(uint64_t total)
{
    if (IsEmpty())
        return;

    Delete(0);
    Delete(total);

    bool clear = false;
    while (!IsEmpty() && !clear)
    {
        clear = true;
        int     lasttype  = MARK_UNSET;
        int64_t lastframe = -1;
        int64_t tempframe = -1;
        frm_dir_map_t::iterator it = m_deleteMap.begin();
        for ( ; it != m_deleteMap.end(); ++it)
        {
            int      thistype  = it.value();
            uint64_t thisframe = it.key();
            if (thisframe >= total)
            {
                Delete(thisframe);
            }
            else if (lasttype == thistype)
            {
                Delete(thistype == MARK_CUT_END ? thisframe :
                                                  (uint64_t)lastframe);
                clear = false;
                break;
            }
            if (MARK_PLACEHOLDER == thistype)
            {
                if (tempframe > 0)
                    Delete(tempframe);
                tempframe = thisframe;
            }
            else
            {
                lasttype  = thistype;
                lastframe = thisframe;
            }
        }
    }
}

/// Use the given map.
void DeleteMap::SetMap(const frm_dir_map_t &map)
{
    Clear();
    m_deleteMap = map;
    m_deleteMap.detach();
    // Can't save an undo point for SetMap() or transcodes fail.
    // Leaving as a marker for refactor.
    //Push(QObject::tr("Set New Cut List"));
}

/// Loads the given commercial break map into the deleteMap.
void DeleteMap::LoadCommBreakMap(uint64_t total, frm_dir_map_t &map)
{
    Clear();
    frm_dir_map_t::Iterator it = map.begin();
    for ( ; it != map.end(); ++it)
        Add(it.key(), it.value() == MARK_COMM_START ?
                MARK_CUT_START : MARK_CUT_END);
    CleanMap(total);
    Push(QObject::tr("Load Detected Commercials"));
}

/// Loads the delete map from the database.
void DeleteMap::LoadMap(uint64_t total, QString undoMessage)
{
    if (!m_ctx || !m_ctx->playingInfo || gCoreContext->IsDatabaseIgnored())
        return;

    Clear();
    m_ctx->LockPlayingInfo(__FILE__, __LINE__);
    m_ctx->playingInfo->QueryCutList(m_deleteMap);
    m_ctx->UnlockPlayingInfo(__FILE__, __LINE__);
    CleanMap(total);
    if (!undoMessage.isEmpty())
        Push(undoMessage);
}

/// Returns true if an auto-save map was loaded.
/// Does nothing and returns false if not.
bool DeleteMap::LoadAutoSaveMap(uint64_t total)
{
    if (!m_ctx || !m_ctx->playingInfo || gCoreContext->IsDatabaseIgnored())
        return false;

    frm_dir_map_t tmpDeleteMap = m_deleteMap;
    Clear();
    m_ctx->LockPlayingInfo(__FILE__, __LINE__);
    bool result = m_ctx->playingInfo->QueryCutList(m_deleteMap, true);
    m_ctx->UnlockPlayingInfo(__FILE__, __LINE__);
    CleanMap(total);
    if (result)
        Push(QObject::tr("Load Auto-saved Cuts"));
    else
        m_deleteMap = tmpDeleteMap;

    return result;
}

/// Saves the delete map to the database.
void DeleteMap::SaveMap(uint64_t total, bool isAutoSave)
{
    if (!m_ctx || !m_ctx->playingInfo || gCoreContext->IsDatabaseIgnored())
        return;

    if (!isAutoSave)
    {
        // Remove temporary placeholder marks
        QMutableMapIterator<uint64_t, MarkTypes> it(m_deleteMap);
        while (it.hasNext())
        {
            it.next();
            if (MARK_PLACEHOLDER == it.value())
            {
                it.remove();
                m_changed = true;
            }
        }

        CleanMap(total);
    }
    m_ctx->LockPlayingInfo(__FILE__, __LINE__);
    m_ctx->playingInfo->SaveMarkupFlag(MARK_UPDATED_CUT);
    m_ctx->playingInfo->SaveCutList(m_deleteMap, isAutoSave);
    m_ctx->UnlockPlayingInfo(__FILE__, __LINE__);
}

/**
 * \brief Resets the internal state tracker.
 *        The tracker records the frame at which the next cut sequence begins.
 *        This is used by the player to avoid iterating over the entire map
 *        many times per second.
 */
void DeleteMap::TrackerReset(uint64_t frame, uint64_t total)
{
    m_nextCutStart = 0;
    m_nextCutStartIsValid = false;
    if (IsEmpty())
        return;

    frm_dir_map_t::iterator cutpoint = m_deleteMap.find(frame);
    if (cutpoint != m_deleteMap.end())
    {
        if (cutpoint.value() == MARK_CUT_START)
        {
            m_nextCutStartIsValid = true;
            m_nextCutStart = cutpoint.key();
        }
        else
        {
            ++cutpoint;
            m_nextCutStartIsValid = (cutpoint != m_deleteMap.end());
            m_nextCutStart = m_nextCutStartIsValid ? cutpoint.key() : total;
        }
    }
    else
        m_nextCutStart = GetNearestMark(frame, total, !IsInDelete(frame),
                                        &m_nextCutStartIsValid);
    LOG(VB_PLAYBACK, LOG_INFO, LOC + QString("Tracker next CUT_START: %1")
                                   .arg(m_nextCutStart));
}

/**
 * \brief Returns true if the given frame has passed the last cut point start
 *        and provides the frame number of the next jump.
 */
bool DeleteMap::TrackerWantsToJump(uint64_t frame, uint64_t total, uint64_t &to)
{
    if (IsEmpty() || !m_nextCutStartIsValid || frame < m_nextCutStart)
        return false;

    to = GetNearestMark(m_nextCutStart, total, true);
    LOG(VB_PLAYBACK, LOG_INFO, LOC +
        QString("Tracker wants to jump to: %1").arg(to));
    return true;
}

/**
 * \brief Returns the number of the last frame in the video that is not in a
 *        cut sequence.
 */
uint64_t DeleteMap::GetLastFrame(uint64_t total) const
{
    uint64_t result = total;
    if (IsEmpty())
        return result;

    frm_dir_map_t::const_iterator it = m_deleteMap.end();
    --it;

    if (it.value() == MARK_CUT_START)
        result = it.key();
    return result;
}

/**
 * \brief Compares the current cut list with the saved cut list
 */
bool DeleteMap::IsSaved(void) const
{
    if (!m_ctx || !m_ctx->playingInfo || gCoreContext->IsDatabaseIgnored())
        return true;

    frm_dir_map_t currentMap(m_deleteMap);
    frm_dir_map_t savedMap;
    m_ctx->LockPlayingInfo(__FILE__, __LINE__);
    m_ctx->playingInfo->QueryCutList(savedMap);
    m_ctx->UnlockPlayingInfo(__FILE__, __LINE__);

    // Remove temporary placeholder marks from currentMap
    QMutableMapIterator<uint64_t, MarkTypes> it(currentMap);
    while (it.hasNext())
    {
        it.next();
        if (MARK_PLACEHOLDER == it.value())
            it.remove();
    }

    return currentMap == savedMap;
}

uint64_t DeleteMap::TranslatePositionAbsToRel(const frm_dir_map_t &deleteMap,
                                              uint64_t absPosition)
{
    uint64_t subtraction = 0;
    uint64_t startOfCutRegion = 0;
    frm_dir_map_t::const_iterator i;
    bool withinCut = false;
    bool first = true;
    for (i = deleteMap.constBegin(); i != deleteMap.constEnd(); ++i)
    {
        if (first)
            withinCut = (i.value() == MARK_CUT_END);
        first = false;
        if (i.key() > absPosition)
            break;
        if (i.value() == MARK_CUT_START && !withinCut)
        {
            withinCut = true;
            startOfCutRegion = i.key();
        }
        else if (i.value() == MARK_CUT_END && withinCut)
        {
            withinCut = false;
            subtraction += (i.key() - startOfCutRegion);
        }
    }
    if (withinCut)
        subtraction += (absPosition - startOfCutRegion);
    return absPosition - subtraction;
}

uint64_t DeleteMap::TranslatePositionRelToAbs(const frm_dir_map_t &deleteMap,
                                              uint64_t relPosition)
{
    uint64_t addition = 0;
    uint64_t startOfCutRegion = 0;
    frm_dir_map_t::const_iterator i;
    bool withinCut = false;
    bool first = true;
    for (i = deleteMap.constBegin(); i != deleteMap.constEnd(); ++i)
    {
        if (first)
            withinCut = (i.value() == MARK_CUT_END);
        first = false;
        if (i.value() == MARK_CUT_START && !withinCut)
        {
            withinCut = true;
            startOfCutRegion = i.key();
            if (relPosition + addition <= startOfCutRegion)
                break;
        }
        else if (i.value() == MARK_CUT_END && withinCut)
        {
            withinCut = false;
            addition += (i.key() - startOfCutRegion);
        }
    }
    return relPosition + addition;
}
