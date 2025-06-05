/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "threads/mmu.h"
#include "vm/file.h"
#define STACK_GROW_RANGE 4192


struct frame_table *frame_table;
bool
lazy_load_segment(struct page *page, void *aux) {
	struct lazy_load_arg *args = aux;
	struct file *file = args->file;
	off_t offset = args->ofs;
	size_t page_read_bytes = args->read_bytes;
	size_t page_zero_bytes = args->zero_bytes;

	file_seek(file, offset);
	if (file_read(file, page->frame->kva, page_read_bytes) != (int)page_read_bytes)
		return false;

	memset(page->frame->kva + page_read_bytes, 0, page_zero_bytes);
	return true;
}

/* 각 서브시스템의 초기화 코드를 호출하여 가상 메모리 서브시스템을 초기화합니다. */
void vm_init(void)
{
	vm_anon_init();
	vm_file_init();
#ifdef EFILESYS /* For project 4 */
	pagecache_init();
#endif
	register_inspect_intr();
	/* 이 위쪽은 수정하지 마세요 !! */
	/* TODO: 이 아래쪽부터 코드를 추가하세요 */

	frame_table_init();
}

/* 페이지의 타입을 가져옵니다. 이 함수는 페이지가 초기화된 후 타입을 알고 싶을 때 유용합니다.
 * 이 함수는 이미 완전히 구현되어 있습니다. */
enum vm_type
page_get_type(struct page *page)
{
	int ty = VM_TYPE(page->operations->type);
	switch (ty)
	{
	case VM_UNINIT:
		return VM_TYPE(page->uninit.type);
	default:
		return ty;
	}
}

void frame_table_init(){
	frame_table = malloc(sizeof(struct frame_table));
	list_init(&frame_table->frame_list);
	
}

/* Helpers */
static struct frame *vm_get_victim(void);
static bool vm_do_claim_page(struct page *page);
static struct frame *vm_evict_frame(void);

/* 초기화 함수와 함께 대기 중인 페이지 객체를 생성합니다. 페이지를 직접 생성하지 말고,
 * 반드시 이 함수나 `vm_alloc_page`를 통해 생성하세요. */
bool vm_alloc_page_with_initializer(enum vm_type type, void *upage, bool writable,
									vm_initializer *init, void *aux)
{

	ASSERT(VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current()->spt;

	/* 이미 해당 page가 SPT에 존재하는지 확인합니다 */
	if (spt_find_page(spt, upage) == NULL)
	{
		/* TODO: VM 타입에 따라 페이지를 생성하고, 초기화 함수를 가져온 뒤,
		 * TODO: uninit_new를 호출하여 "uninit" 페이지 구조체를 생성하세요.
		 * TODO: uninit_new 호출 후에는 필요한 필드를 수정해야 합니다. */
		bool (*page_initializer)(struct page *, enum vm_type, void *kva);
		struct page *page = malloc(sizeof(struct page));

		switch (VM_TYPE(type))
		{
		case VM_ANON:
			page_initializer = anon_initializer;
			break;
		case VM_MMAP:
			/* 매핑 카운트를 추가해두자
			   mmap_list로 mmap 페이지를 관리할거면 필요 x */
		case VM_FILE:
			page_initializer = file_backed_initializer;
			break;
		default:
			free(page);
			goto err;
			break;
		}

		uninit_new(page, upage, init, type, aux, page_initializer);
		page->writable=writable;
		/* TODO: 생성한 페이지를 spt에 삽입하세요. */
		return spt_insert_page(spt, page);
		
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
/* 가상 주소를 통해 SPT에서 페이지를 찾아 리턴합니다.
 * 에러가 발생하면 NULL을 리턴하세요 */
struct page *
spt_find_page(struct supplemental_page_table *spt, void *va)
{
    ASSERT(spt != NULL);
    // ASSERT(va != NULL);
	if(va==NULL) return NULL;

	struct page temp;
	temp.va = pg_round_down(va);
	
	struct hash_elem *e = hash_find(&spt->spt_hash, &temp.hash_elem);
	
	if (e == NULL)
		return NULL;
	else
		return hash_entry(e, struct page, hash_elem);
	
}


/* Insert PAGE into spt with validation. */
bool spt_insert_page(struct supplemental_page_table *spt,
					 struct page *page)
{
	int succ = false;
	ASSERT(page!=NULL);
	struct hash_elem * e=hash_insert(&spt->spt_hash, &page->hash_elem);
	if(e!=NULL) return succ; //실패했음

	succ=true;
	return succ;
}

void spt_remove_page(struct supplemental_page_table *spt, struct page *page)
{
	hash_delete(&spt->spt_hash, &page->hash_elem);	
	vm_dealloc_page(page);

}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim(void)
{
	struct frame *victim;
	/* TODO: 교체 정책을 여기서 구현해서 희생자 페이지 찾기 */

	victim= list_pop_front(&frame_table->frame_list);

	return victim;
}

/* 한 페이지를 교체(evict)하고 해당 프레임을 반환합니다.
 * 에러가 발생하면 NULL을 반환합니다.*/
static struct frame *
vm_evict_frame(void)
{
	struct frame *victim UNUSED = vm_get_victim();


	/** TODO: 여기서 swap_out 매크로를 호출??
	 *	pml4_clear_page를 아마 사용?? (잘 모름)
	 */

	return NULL;
}

/* palloc()을 사용하여 프레임을 할당합니다.
 * 사용 가능한 페이지가 없으면 페이지를 교체(evict)하여 반환합니다.
 * 이 함수는 항상 유효한 주소를 반환합니다. 즉, 사용자 풀 메모리가 가득 차면,
 * 이 함수는 프레임을 교체하여 사용 가능한 메모리 공간을 확보합니다.*/
static struct frame *
vm_get_frame(void)
{
	struct frame *frame = malloc(sizeof(struct frame));
	frame->kva= palloc_get_page(PAL_USER | PAL_ZERO);
	frame->page=NULL;
	if(frame ==NULL){
		// frame=vm_evict_frame(); //이 안에서 swap out
		PANIC("TODO");
	}

	ASSERT(frame != NULL);
	ASSERT(frame->page == NULL);

	list_push_back(frame_table, &frame->frame_elem);
	
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth(void *addr)
{
	/* 스택 최하단에 익명 페이지를 추가하여 사용
	 * addr은 PGSIZE로 내림(정렬)하여 사용	 */
	// uint64_t *address = (uint64_t *)pg_round_down(addr);
	vm_alloc_page(VM_ANON, addr, true); // 스택 최하단에 익명 페이지 추가
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp(struct page *page UNUSED)
{
}

/* Return true on success */
/* 인터럽트 프레임, addr=폴트를 일으킨 주소(코드일 수도있고 데이터일수도 있음),
user=사용자 접근인지 커널 접근인지, write=true면 쓰기 허용 false면 읽기만
not_present: true면 존재하지 않는 페이지, false면  */
bool vm_try_handle_fault(struct intr_frame *f , void *addr ,
						 bool user UNUSED, bool write UNUSED, bool not_present )
{

	// ASSERT(addr!=NULL);
	if (!not_present) return false;
    if (!is_user_vaddr(addr)) return false;

    struct supplemental_page_table *spt = &thread_current()->spt;
	// addr = pg_round_down(addr);
    struct page *page = spt_find_page(spt, addr);
	uintptr_t rsp = thread_current()->user_rsp; // 유저 스택의 rsp 가져오기

	if(page){
		return vm_do_claim_page(page);
	}

    if (page == NULL) {
        if (addr > rsp - PGSIZE && addr >= USER_STACK - (1 << 20) && addr < USER_STACK) {
            vm_stack_growth(pg_round_down(addr));
			return true;
		}
        
        return false;
	}
}

/* Free the page.
프레임 해제, 파일 wriet-back, 페이지 테이블 매핑 해제 등 모든 자원 정리 수행 
 * DO NOT MODIFY THIS FUNCTION. */
void vm_dealloc_page(struct page *page)
{
	destroy(page);
	free(page);
}

/* VA에 할당된 페이지를 요구합니다 . */
bool vm_claim_page(void *va)
{
	struct page *page = spt_find_page(&thread_current()->spt, va);
	/* TODO: Fill this function */
	if(page==NULL) return false;

	return vm_do_claim_page(page);
}

/* PAGE를 요구하고 mmu를 설정합니다*/
static bool
vm_do_claim_page(struct page *page)
{
	struct frame *frame = vm_get_frame();
	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	if(!pml4_set_page(thread_current()->pml4, page->va, frame->kva, page->writable)){
		// swap-out? 
		PANIC("TODO");
	}
	

	return swap_in(page, frame->kva);
}

bool is_less(const struct hash_elem *a, const struct hash_elem *b, void *aux){
	if(a==NULL) return true;
	else if (b==NULL) return true;
	const struct page *page_a=hash_entry(a, struct page, hash_elem);
	const struct page *page_b=hash_entry(b, struct page, hash_elem);
	return page_a->va < page_b->va;
}

size_t page_hash(const struct hash_elem *e, void * aux){
	const struct page *p = hash_entry(e, struct page, hash_elem);
	return hash_bytes(&p->va, sizeof p->va);
}

/* Initialize new supplemental page table */
void supplemental_page_table_init(struct supplemental_page_table *spt)
{
	if(!hash_init(&spt->spt_hash, page_hash, is_less, NULL))
		return;
}



// /* Copy supplemental page table from src to dst */
// bool supplemental_page_table_copy(struct supplemental_page_table *dst UNUSED,
// 								  struct supplemental_page_table *src UNUSED)
// {
// 	struct hash_iterator i;
// 	hash_first(&i, &src->spt_hash);
// 	while(hash_next(&i)){
// 		struct page *src_page = hash_entry(hash_cur(&i), struct page, hash_elem);
// 		void *upage = src_page->va;

// 		if(src_page->operations->type == VM_UNINIT){
// 			struct uninit_page *uninit = &src_page->uninit;
// 			if(!vm_alloc_page_with_initializer(uninit->type, upage, src_page->writable,uninit->init,uninit->aux)){
// 				return false;
// 			}
// 		} else{

// 			if(!vm_alloc_page_with_initializer(src_page->operations->type, upage, src_page->writable,NULL, NULL) || !vm_claim_page(upage)){
// 				return false;
// 			}
// 		}
// 		struct page *dst_page = spt_find_page(dst, upage);
// 		memcpy(dst_page->frame->kva, src_page->frame->kva, PGSIZE);
// 	}
// 	return true;
// }


static void *duplicate_aux(struct page *src_page)
{
   
	struct file_info *src_info = (struct file_info *)src_page->uninit.aux;
	struct file_info *dst_info = malloc(sizeof(struct file_info));

	dst_info->file = file_reopen(src_info->file);
	dst_info->ofs = src_info->ofs;
	dst_info->read_bytes = src_info->read_bytes;
	dst_info->zero_bytes = src_info->zero_bytes;

	return dst_info;
   
}

bool supplemental_page_table_copy(struct supplemental_page_table *dst , struct supplemental_page_table *src )
{
    struct hash_iterator i;
    hash_first(&i, &src->spt_hash);
    struct thread *cur = thread_current();

    while (hash_next(&i))
    {
        struct page *src_page = hash_entry(hash_cur(&i), struct page, hash_elem);
        enum vm_type type = src_page->operations->type;
        void *upage = src_page->va;
        bool writable = src_page->writable;

        /* 1) type이 uninit이면 */
        if (type == VM_UNINIT)
        {
            enum vm_type reserved_type = src_page->uninit.type;
            vm_initializer *init = src_page->uninit.init;
            void *aux = duplicate_aux(src_page);
         
            if (!vm_alloc_page_with_initializer(reserved_type, upage, writable, init, aux))
                return false;
            continue;
        }

        /* 2) type이 file이면 */
        if (type == VM_FILE)
        {
            struct lazy_load_arg *file_aux = malloc(sizeof(struct lazy_load_arg));
            file_aux->file = src_page->file.file;
            file_aux->ofs = src_page->file.ofs;
            file_aux->read_bytes = src_page->file.read_bytes;
            file_aux->zero_bytes = src_page->file.zero_bytes;

            if (!vm_alloc_page_with_initializer(type, upage, writable, NULL, file_aux))
                return false;

            struct page *file_page = spt_find_page(dst, upage);
            file_backed_initializer(file_page, type, NULL);
            file_page->frame = src_page->frame;

            pml4_set_page(cur->pml4, file_page->va, src_page->frame->kva, writable);
            continue;
        }

        /* 3) 그 외 타입 */
        if (!vm_alloc_page(type, upage, writable))
            return false;

        if (!vm_claim_page(upage))
            return false;

        struct page *dst_page = spt_find_page(dst, upage);
        memcpy(dst_page->frame->kva, src_page->frame->kva, PGSIZE);
    }
    return true;
}


void page_desturctor(struct hash_elem *e, void * aux){
	struct page *p = hash_entry(e, struct page, hash_elem);
	vm_dealloc_page(p);
}


/* Free the resource hold by the supplemental page table */
void supplemental_page_table_kill(struct supplemental_page_table *spt)
{
	/*hash 테이블 순회하면서 동시에 엔트리 삭제(hash_delete)하면 안됨
	그럼 내부 구조가 바뀌어버리니 iterator가 안전하게 동작 하지 않음 
	*/
	// hash_destroy(&spt->spt_hash, page_desturctor);
	hash_clear(&spt->spt_hash, page_desturctor);
}